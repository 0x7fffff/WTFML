/**
 * @file ImageDecode.cpp
 * @brief ImageDecode 的实现：Qt 先试，剩下的（webp）交给 WIC。见头文件里那两段 NOTE。
 * @author 0x7fffff
 * @date 2026-09-23
 */
#include "ImageDecode.hpp"

#include <QString>

#include <string>
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shlwapi.h>   // SHCreateMemStream
#  include <wincodec.h>  // WIC
#endif

namespace ImageDecode {

#ifdef Q_OS_WIN

namespace {

/// WIC 的工厂，进程里就一份。**故意不释放**：它能活到进程结束（和 main.cpp 里那个
/// QFile、Qt 自己那些进程级单例一个道理），收它反而要挑一个"确定没人再用了"的时刻。
///
/// NOTE: COM 先起一遍。Qt 在主线程上本来就可能已经起过（剪贴板、拖放要 OLE），那时这里
///       回 `S_FALSE`；模型不一样时回 `RPC_E_CHANGED_MODE` —— **两种都不影响下面这句**，
///       WIC 在 STA / MTA 下都能用，所以刻意不判它的返回值，也不配对 CoUninitialize
///      （起 COM 的不是我们，生命周期就不该由我们收）。
IWICImagingFactory *wicFactory() {
    static IWICImagingFactory *factory = []() -> IWICImagingFactory * {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IWICImagingFactory *created = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&created)))) {
            return nullptr;
        }
        return created;
    }();
    return factory;
}

/// WIC 那串解码器里有没有一条认 `.webp` 的。
///
/// 为什么是"问它认哪些后缀"而不是"拿一段 webp 试解一下"：枚举是只读的、一次就够，而
/// 试解要么得内嵌一段几十字节的合法 webp（还得保证它永远合法），要么得等真有图才试
/// —— 后者会让第一张图白白多等一个超时。
bool wicHasWebpDecoder(IWICImagingFactory *factory) {
    IEnumUnknown *enumerator = nullptr;
    if (FAILED(factory->CreateComponentEnumerator(WICDecoder, WICComponentEnumerateDefault,
                                                  &enumerator))) {
        return false;
    }

    bool found = false;
    IUnknown *unknown = nullptr;
    ULONG fetched = 0;
    while (!found && enumerator->Next(1, &unknown, &fetched) == S_OK && fetched == 1) {
        IWICBitmapDecoderInfo *info = nullptr;
        if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&info))) && info != nullptr) {
            // 先问长度、再按长度要缓冲（这一串形如 `.jpg,.jpeg` 或 `.webp`）。
            // NOTE: 缓冲用 std::wstring 而不是 QString —— WIC 要的是 `wchar_t*`，
            //       而 QString 那个 `data()` 是 QChar*（虽然 Windows 上两者都是两字节，
            //       拿类型强转去糊是要挨严格别名规则的）。
            UINT length = 0;
            if (SUCCEEDED(info->GetFileExtensions(0, nullptr, &length)) && length > 0) {
                std::wstring extensions(length, L'\0');
                if (SUCCEEDED(info->GetFileExtensions(length, extensions.data(), &length))) {
                    found = QString::fromWCharArray(extensions.c_str())
                                .contains(QStringLiteral(".webp"), Qt::CaseInsensitive);
                }
            }
            info->Release();
        }
        unknown->Release();
    }
    enumerator->Release();
    return found;
}

/// 走 WIC 解一串字节。**是内存流，不落盘** —— 产品的图标正是 QNetworkReply 里的一串字节，
/// 写临时文件那条路在这里不成立（见 build/tools/wicprobe/ 里那份验过这一点的探针）。
QImage decodeWithWic(const QByteArray &bytes) {
    IWICImagingFactory *factory = wicFactory();
    if (factory == nullptr) {
        return QImage();
    }

    IStream *stream = SHCreateMemStream(reinterpret_cast<const BYTE *>(bytes.constData()),
                                        static_cast<UINT>(bytes.size()));
    if (stream == nullptr) {
        return QImage();
    }

    QImage image;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    if (SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand,
                                                   &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeCustom))) {
        UINT width = 0;
        UINT height = 0;
        frame->GetSize(&width, &height);
        if (width > 0 && height > 0) {
            // 32bppBGRA（直存 alpha）在内存里的字节顺序正好就是 QImage::Format_ARGB32
            // 要的那个 —— 别换成 Premultiplied，那是另一个格式。
            QImage buffer(static_cast<int>(width), static_cast<int>(height),
                          QImage::Format_ARGB32);
            // NOTE: 分配不出来时 buffer 是**空图**（sizeInBytes() = 0、bits() = nullptr）——
            //       尺寸大得离谱的图能走到这里（这份字节是从网上来的，按外部输入对待），
            //       而且 QImage 的构造对"宽度乘高度乘 4 溢出"这种情况也只是回一张空图。
            //       空图这一路上有两处后果：拿 nullptr 去 CopyPixels 至少是白跑一趟；
            //       更要紧的是下面那句 `image = buffer` 会把一张**空图**当成"解出来的图"
            //       带出去 —— 调用方看 isNull() 判不出这是"没解出来"还是"解出来是空的"，
            //       真出问题时现场就成了"这张图明明下到了却画不出来"，而且没有任何日志。
            if (!buffer.isNull()) {
                const UINT stride = width * 4;
                if (SUCCEEDED(converter->CopyPixels(nullptr, stride,
                                                    static_cast<UINT>(buffer.sizeInBytes()),
                                                    buffer.bits()))) {
                    image = buffer; // QImage 是隐式共享的：这一次浅拷贝就让 buffer 那块内存
                                    // 有了第二个主人，buffer 出作用域也不影响它。
                }
            }
        }
    }

    if (converter != nullptr) {
        converter->Release();
    }
    if (frame != nullptr) {
        frame->Release();
    }
    if (decoder != nullptr) {
        decoder->Release();
    }
    stream->Release();
    return image;
}

} // namespace

bool webpAvailable() {
    static const bool available = []() {
        IWICImagingFactory *factory = wicFactory();
        return factory != nullptr && wicHasWebpDecoder(factory);
    }();
    return available;
}

#else

bool webpAvailable() { return false; }

#endif // Q_OS_WIN

QImage decode(const QByteArray &bytes) {
    if (bytes.isEmpty()) {
        return QImage(); // 空的一串不是"一张空图"，是"没有图"
    }

    // 先让 Qt 自己试。绝大多数的图走到这里就结束了（png / jpg / gif / bmp / ico…）。
    QImage image;
    if (image.loadFromData(bytes)) {
        return image;
    }

#ifdef Q_OS_WIN
    // 剩下的就是 Qt 不认的。只有确信这台机器解得了才把它递过去 —— 省一次必然失败的
    // COM 往返，也让"能不能解"这件事只有一个判据（见头文件）。
    if (webpAvailable()) {
        return decodeWithWic(bytes);
    }
#endif
    return QImage();
}

} // namespace ImageDecode
