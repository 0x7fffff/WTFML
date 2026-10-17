#include "ResumableDownload.hpp"
#include "DownloadManager.hpp"
#include "DownloadRetryPolicy.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <algorithm>

namespace {
QString digest(const QString &path, const QSharedPointer<QAtomicInt> &abort) {
    if (QFileInfo(path).isSymLink()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (abort->loadRelaxed()) return {};
        const auto bytes = file.read(64 * 1024);
        if (bytes.isEmpty()) return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
QString hashText(const QByteArray &text) { return QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex()); }
void removeParts(const QString &directory) {
    // 只删除本协调器命名的文件，不递归删除目录，不跟随符号链接。
    static const QRegularExpression name(QStringLiteral("^part-[0-9]+\\.bin$"));
    for (const auto &file : QDir(directory).entryList({QStringLiteral("part-*.bin")}, QDir::Files | QDir::Hidden | QDir::System))
        if (name.match(file).hasMatch()) QFile::remove(QDir(directory).filePath(file));
    QFile::remove(QDir(directory).filePath(QStringLiteral("manifest.json")));
}
bool strongEtag(const QByteArray &value) {
    return value.size() >= 2 && value.size() <= 4096 && value.startsWith('"') && value.endsWith('"') && !value.contains('\r') && !value.contains('\n');
}
}

ResumableDownload::ResumableDownload(QObject *parent) : QObject(parent) {}
ResumableDownload::~ResumableDownload() { cancel(); }

QString ResumableDownload::cacheDirectory(const QUrl &url, const QString &target, const QString &root) {
    const QString base = root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/range-downloads") : root;
    return QDir(base).filePath(hashText(url.toEncoded() + '\n' + QFileInfo(target).absoluteFilePath().toUtf8()));
}

void ResumableDownload::start(const QUrl &url, const QString &target, const QString &sha1, qint64 size, bool overwrite, const QString &cacheRoot) {
    if (!cancel()) { emit statusChanged(tr("正在提交完整文件，请等待保存结束。")); return; }
    delete m_files;
    m_files = new DownloadManager(this);
    m_files->setUseMirror(false);
    m_files->setMaxConcurrentDownloads(4);
    m_abort = QSharedPointer<QAtomicInt>::create(0);
    m_running = true; m_stage = Stage::Probe;
    m_url = url; m_target = target; m_sha1 = sha1; m_expectedSize = size; m_overwrite = overwrite;
    m_total = -1; m_count = 0; m_complete.clear(); m_live.clear(); m_etag.clear();
    m_urlHash = hashText(url.toEncoded()); m_cache = cacheDirectory(url, target, cacheRoot);
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) || !QDir::isAbsolutePath(target)) {
        finish(false, tr("下载地址或保存路径无效。")); return;
    }
    if (QFileInfo(m_cache).isSymLink() || !QDir().mkpath(m_cache)) { finish(false, tr("续传缓存目录不可用。")); return; }
    m_lock = QSharedPointer<QLockFile>::create(QDir(m_cache).filePath(QStringLiteral("lock")));
    if (!m_lock->tryLock(0)) { finish(false, tr("同一下载的缓存正被其他任务使用，请稍后重试。")); return; }
    connect(m_files, &DownloadManager::downloadProgress, this, [this](const QString &file, qint64 done, qint64 total) {
        if (!m_running) return;
        if (m_stage == Stage::Single) { emit progress(done, total); return; }
        if (m_stage != Stage::Parts) return;
        bool valid = false;
        const int index = file.mid(5, file.size() - 9).toInt(&valid);
        if (valid && index >= 0 && index < m_count && !m_complete.contains(index)) {
            m_live[index] = std::clamp<qint64>(done, 0, partLength(index)); emitProgress();
        }
    });
    connect(m_files, &DownloadManager::downloadFinished, this, [this](const QString &file, bool success, const QString &error) {
        if (!m_running) return;
        if (m_stage == Stage::Single) {
            if (success) clearCacheFiles();
            finish(success, error); return;
        }
        if (m_stage != Stage::Parts) return;
        if (!success) {
            if (error.startsWith(QStringLiteral("range:"))) {
                m_stage = Stage::Switching;
                const auto generation = m_generation;
                QTimer::singleShot(0, this, [this, generation] { if (m_running && generation == m_generation) startSingle(); });
            } else finish(false, error);
            return;
        }
        bool valid = false;
        const int index = file.mid(5, file.size() - 9).toInt(&valid);
        if (!valid || index < 0 || index >= m_count) { finish(false, tr("分段编号无效。")); return; }
        const auto hash = digest(partPath(index), m_abort); // 每段上限 4 MiB；重启全量校验在后台。
        if (hash.isEmpty()) { finish(false, tr("无法校验已下载的分段。")); return; }
        m_complete[index] = hash; m_live.remove(index);
        QString problem;
        if (!saveManifest(&problem)) { finish(false, problem); return; }
        emit checkpointSaved(completedBytes()); emitProgress();
        if (m_complete.size() == m_count) {
            m_stage = Stage::Switching;
            const auto generation = m_generation;
            QTimer::singleShot(0, this, [this, generation] { if (m_running && generation == m_generation) assemble(); });
        }
    });
    emit statusChanged(tr("正在检查分段与续传支持…"));
    probe();
}

void ResumableDownload::probe(int attempt) {
    if (!m_running) return;
    QNetworkRequest request(m_url);
    request.setRawHeader("Range", "bytes=0-0"); request.setRawHeader("Accept-Encoding", "identity");
    request.setTransferTimeout(15000);
    auto *reply = m_files->networkAccess()->get(request);
    m_probe = reply;
    reply->setReadBufferSize(4096);
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply] {
        // 不支持 Range 的服务器可能开始返回几 GB 正文；立即停止探测，不把它读进内存。
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        static const QRegularExpression range(QStringLiteral("^bytes 0-0/[0-9]+$"));
        if (status == 200 || (status == 206 && !range.match(QString::fromLatin1(reply->rawHeader("Content-Range"))).hasMatch())) {
            reply->setProperty("rangeUnsupported", true); reply->abort();
        }
    });
    connect(reply, &QIODevice::readyRead, this, [reply] {
        auto data = reply->property("probeBytes").toByteArray();
        data += reply->read(qMax<qint64>(0, 2 - data.size()));
        reply->setProperty("probeBytes", data);
        if (data.size() > 1) { reply->setProperty("rangeUnsupported", true); reply->abort(); }
    });
    QTimer::singleShot(20000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, attempt] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto code = reply->error();
        const auto header = reply->rawHeader("Content-Range");
        const auto etag = reply->rawHeader("ETag");
        const bool unsupported = reply->property("rangeUnsupported").toBool() || status == 200 || status == 400 || status == 405 || status == 416 || status == 501;
        auto data = reply->property("probeBytes").toByteArray();
        if (data.isEmpty()) data = reply->read(2);
        const auto reason = reply->errorString();
        const auto retryAfter = reply->rawHeader("Retry-After");
        const auto effectiveUrl = reply->url();
        reply->deleteLater();
        if (generation != m_generation || !m_running) return;
        m_probe = nullptr;
        if (unsupported) { startSingle(); return; }
        if (code != QNetworkReply::NoError) {
            const int delay = DownloadRetryPolicy::transient(code, status) ? DownloadRetryPolicy::delayMs(attempt, retryAfter) : -1;
            if (delay >= 0) { QTimer::singleShot(delay, this, [this, generation, attempt] { if (m_running && generation == m_generation) probe(attempt + 1); }); return; }
            finish(false, reason); return;
        }
        static const QRegularExpression pattern(QStringLiteral("^bytes 0-0/([0-9]+)$"));
        const auto match = pattern.match(QString::fromLatin1(header));
        bool valid = false;
        const qint64 total = match.captured(1).toLongLong(&valid);
        if (status != 206 || !valid || data.size() != 1 || total <= 0) { startSingle(); return; }
        if (m_expectedSize > 0 && total != m_expectedSize) { finish(false, tr("源站文件大小与预期不一致。")); return; }
        m_total = total;
        m_source = effectiveUrl;
        m_urlHash = hashText(m_url.toEncoded() + '\n' + effectiveUrl.toEncoded());
        if (strongEtag(etag)) m_etag = etag;
        // 没有强校验依据时不跨请求拼接，以免源文件变化而大小未变。
        // 先限制大小再做向上取整，避免恶意 Content-Range 的整数溢出。
        if (total < 4 * 1024 * 1024 || total > 8192LL * 4 * 1024 * 1024 ||
            (m_etag.isEmpty() && m_sha1.isEmpty())) { startSingle(); return; }
        m_partSize = std::max<qint64>(1024 * 1024, std::min<qint64>(4 * 1024 * 1024, (total + 3) / 4));
        if ((total + m_partSize - 1) / m_partSize > 8192) { startSingle(); return; }
        m_count = int((total + m_partSize - 1) / m_partSize);
        prepareCache();
    });
}

QString ResumableDownload::partPath(int index) const { return QDir(m_cache).filePath(QStringLiteral("part-%1.bin").arg(index)); }
qint64 ResumableDownload::partLength(int index) const { return std::min(m_partSize, m_total - index * m_partSize); }
qint64 ResumableDownload::completedBytes() const { qint64 result = 0; for (auto it = m_complete.cbegin(); it != m_complete.cend(); ++it) result += partLength(it.key()); return result; }
void ResumableDownload::emitProgress() { qint64 result = completedBytes(); for (auto bytes : m_live) result += bytes; emit progress(result, m_total); }

void ResumableDownload::prepareCache() {
    m_stage = Stage::Prepare;
    emit statusChanged(tr("正在后台校验续传缓存…"));
    struct Result { QMap<int, QString> parts; };
    auto result = QSharedPointer<Result>::create();
    const auto cache = m_cache, identity = m_urlHash, sha1 = m_sha1;
    const auto etag = m_etag;
    const auto total = m_total, partSize = m_partSize;
    const int count = m_count;
    const auto token = m_abort; const auto lock = m_lock; const auto generation = m_generation;
    auto *worker = QThread::create([=] {
        QFile state(QDir(cache).filePath(QStringLiteral("manifest.json")));
        QJsonObject json;
        if (state.open(QIODevice::ReadOnly) && state.size() < 4 * 1024 * 1024) json = QJsonDocument::fromJson(state.readAll()).object();
        state.close();
        if (json.value("schema").toInt() != 1 || json.value("urlHash").toString() != identity ||
            json.value("etag").toString().toLatin1() != etag || json.value("sha1").toString() != sha1 ||
            qint64(json.value("total").toDouble()) != total || qint64(json.value("partSize").toDouble()) != partSize) {
            removeParts(cache); return;
        }
        for (const auto &value : json.value("parts").toArray()) {
            if (token->loadRelaxed()) return;
            const auto item = value.toObject(); const int index = item.value("index").toInt(-1);
            if (index < 0 || index >= count) continue;
            const auto file = QDir(cache).filePath(QStringLiteral("part-%1.bin").arg(index));
            const auto hash = item.value("sha256").toString();
            if (hash.size() == 64 && QFileInfo(file).size() == std::min(partSize, total - index * partSize) && digest(file, token) == hash)
                result->parts[index] = hash;
        }
        Q_UNUSED(lock);
    });
    connect(worker, &QThread::finished, this, [this, result, generation] {
        if (!m_running || generation != m_generation) return;
        m_complete = result->parts;
        emit statusChanged(m_complete.isEmpty() ? tr("正在并行下载分段…") : tr("已恢复 %1 个完整分段，继续下载剩余部分…").arg(m_complete.size()));
        downloadParts();
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

bool ResumableDownload::saveManifest(QString *error) {
    QJsonArray parts;
    for (auto it = m_complete.cbegin(); it != m_complete.cend(); ++it) parts.append(QJsonObject{{"index", it.key()}, {"sha256", it.value()}});
    const QJsonObject state{{"schema", 1}, {"urlHash", m_urlHash}, {"etag", QString::fromLatin1(m_etag)}, {"sha1", m_sha1},
                            {"total", double(m_total)}, {"partSize", double(m_partSize)}, {"parts", parts}};
    QSaveFile file(QDir(m_cache).filePath(QStringLiteral("manifest.json")));
    const auto bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = tr("无法保存续传记录：%1").arg(file.errorString());
        return false;
    }
    return true;
}

void ResumableDownload::downloadParts() {
    if (m_complete.size() == m_count) { assemble(); return; }
    m_stage = Stage::Parts;
    QString problem;
    if (!saveManifest(&problem)) { finish(false, problem); return; }
    emitProgress();
    m_files->setAllowOverwrite(true);
    m_files->beginBatch();
    for (int index = 0; index < m_count; ++index) {
        if (m_complete.contains(index)) continue;
        DownloadRange range{index * m_partSize, index * m_partSize + partLength(index) - 1, m_total, m_etag};
        m_files->downloadFile(m_source.toString(), partPath(index), {}, partLength(index), VerifyMode::AlwaysDownload, {}, range);
    }
    m_files->endBatch();
}

void ResumableDownload::startSingle() {
    m_stage = Stage::Switching;
    m_files->cancelBatch();
    m_stage = Stage::Single;
    emit statusChanged(tr("源站未满足安全分段条件，使用普通下载与有限重试。"));
    m_files->setAllowOverwrite(m_overwrite);
    m_files->beginBatch();
    m_files->downloadFile(m_url.toString(), m_target, m_sha1, m_expectedSize, VerifyMode::AlwaysDownload);
    m_files->endBatch();
}

void ResumableDownload::assemble() {
    m_stage = Stage::Assemble;
    emit statusChanged(tr("所有分段已就绪，正在后台合并、校验并保存…"));
    auto result = QSharedPointer<QString>::create();
    const auto cache = m_cache, target = m_target, expectedHash = m_sha1;
    const auto complete = m_complete;
    const auto token = m_abort;
    const auto lock = m_lock;
    const auto generation = m_generation;
    const auto total = m_total; const bool overwrite = m_overwrite;
    auto *worker = QThread::create([=] {
        Q_UNUSED(lock);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QSaveFile output(target); output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) { *result = output.errorString(); return; }
        QCryptographicHash whole(QCryptographicHash::Sha1);
        qint64 written = 0; QByteArray prefix;
        for (auto it = complete.cbegin(); it != complete.cend(); ++it) {
            const QString path = QDir(cache).filePath(QStringLiteral("part-%1.bin").arg(it.key()));
            QFile part(path);
            if (QFileInfo(path).isSymLink() || !part.open(QIODevice::ReadOnly)) { *result = QStringLiteral("续传分段不可读。"); return; }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while (!part.atEnd()) {
                if (token->loadRelaxed()) return;
                const auto data = part.read(64 * 1024);
                if (data.isEmpty() || output.write(data) != data.size()) { *result = QStringLiteral("无法合并分段。"); return; }
                if (prefix.size() < 4) prefix += data.left(4 - prefix.size());
                hash.addData(data); whole.addData(data); written += data.size();
            }
            if (QString::fromLatin1(hash.result().toHex()) != it.value()) { *result = QStringLiteral("续传缓存已经改变，请重试以重新下载损坏分段。"); return; }
        }
        if (written != total || (!expectedHash.isEmpty() && QString::fromLatin1(whole.result().toHex()).compare(expectedHash, Qt::CaseInsensitive))) {
            *result = QStringLiteral("合并后的大小或 SHA-1 校验失败。"); return;
        }
        const auto ext = QFileInfo(target).suffix().toLower();
        if ((ext == QStringLiteral("jar") || ext == QStringLiteral("zip")) && expectedHash.isEmpty() &&
            prefix != QByteArray("PK\x03\x04", 4) && prefix != QByteArray("PK\x05\x06", 4) && prefix != QByteArray("PK\x07\x08", 4)) {
            *result = QStringLiteral("下载内容不是有效的 ZIP/JAR 文件。"); return;
        }
        if (token->loadRelaxed()) return;
        if (!overwrite && (QFileInfo::exists(target) || QFileInfo(target).isSymLink())) { *result = QStringLiteral("目标文件已出现，未覆盖。"); return; }
        // 取消和原子提交只有一个能获准：接受取消后绝不再替换旧目标。
        if (!token->testAndSetOrdered(0, 2)) return;
        if (!output.commit()) *result = output.errorString();
        else token->storeRelease(3);
    });
    connect(worker, &QThread::finished, this, [this, result, generation] {
        if (!m_running || generation != m_generation) return;
        if (result->isEmpty()) { clearCacheFiles(); emit progress(m_total, m_total); }
        finish(result->isEmpty(), *result);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void ResumableDownload::clearCacheFiles() const { removeParts(m_cache); }
void ResumableDownload::finish(bool success, const QString &error) {
    if (!m_running) return;
    m_running = false; m_stage = Stage::Done;
    if (!success && m_files) m_files->cancelBatch();
    m_lock.reset();
    emit finished(success, error);
}
bool ResumableDownload::canCancel() const {
    return m_running && (m_stage != Stage::Assemble || !m_abort || m_abort->loadAcquire() < 2);
}
bool ResumableDownload::cancel() {
    if (m_running && m_stage == Stage::Assemble && m_abort &&
        !m_abort->testAndSetOrdered(0, 1) && m_abort->loadAcquire() >= 2) return false;
    ++m_generation;
    m_running = false; m_stage = Stage::Done;
    if (m_abort) m_abort->storeRelaxed(1);
    if (m_probe) { m_probe->abort(); m_probe = nullptr; }
    if (m_files) m_files->cancelBatch();
    m_lock.reset();
    return true;
}
