#include "JavaManageChecks.hpp"

#include "JavaManageDialog.hpp"
#include "settings/GlobalJavaSettings.hpp"
#include <QFile>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <cstdio>

int runJavaManageChecks() {
    int failures = 0;
    const auto check = [&](bool ok, const char *name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    JavaManageDialog page;
    check(JavaManageDialog::managedMajors() == QList<int>{25, 21, 17, 11, 8},
          "managed majors include Java 11 in descending order");
    int downloaded = 0, selected = 0;
    QObject::connect(&page, &JavaManageDialog::downloadRequested, &page, [&](int major) { downloaded = major; });
    QObject::connect(&page, &JavaManageDialog::useRuntimeRequested, &page, [&](int major) { selected = major; });
    for (int major : JavaManageDialog::managedMajors()) {
        auto *download = page.findChild<QPushButton *>(JavaManageDialog::rowButtonName(major));
        auto *use = page.findChild<QPushButton *>(JavaManageDialog::useButtonName(major));
        auto *label = page.findChild<QLabel *>(JavaManageDialog::rowLabelName(major));
        check(download && use && label, "each managed major has its own controls");
        if (!download || !use || !label) continue;
        check(!page.isInstalled(major) && use->isHidden(), "missing runtime cannot be selected");
        download->click();
        check(downloaded == major, "download action forwards the exact major");
        const QString path = QStringLiteral("C:/runtime/%1/bin/java.exe").arg(major);
        page.setRuntime(major, path);
        page.setSelection(false, path);
        check(page.isInstalled(major) && !use->isHidden() && label->text().contains(JavaManageDialog::usedMarkText()),
              "installed runtime can be selected and marked current");
        page.setSelection(true, {});
        use->click();
        check(selected == major, "selection forwards the exact major");
        page.setBusy(true);
        check(!download->isEnabled() && !use->isEnabled(), "busy state also protects additional majors");
        page.setBusy(false);
    }
    check(!page.hasMissing(), "all managed runtimes satisfy download-all state");
    page.setRuntime(11, {});
    check(page.hasMissing() && !page.isInstalled(11), "removing Java 11 makes batch download available");
    QTemporaryDir fixture;
    check(fixture.isValid(), "runtime version fixture available");
    if (fixture.isValid()) {
        for (int major : {11, 16}) {
            const QString root = fixture.path() + QStringLiteral("/%1").arg(major);
            QDir().mkpath(root + QStringLiteral("/bin"));
            QFile exe(root + QStringLiteral("/bin/java.exe"));
            QFile release(root + QStringLiteral("/release"));
            const bool ready = exe.open(QIODevice::WriteOnly) && release.open(QIODevice::WriteOnly);
            if (ready) release.write(QStringLiteral("JAVA_VERSION=\"%1.0.1\"\n").arg(major).toUtf8());
            exe.close(); release.close();
            check(ready && GlobalJavaSettings::probeJavaMajorVersion(exe.fileName()) == major,
                  "local runtime metadata recognizes the additional exact major");
        }
    }
    return failures ? 1 : 0;
}
