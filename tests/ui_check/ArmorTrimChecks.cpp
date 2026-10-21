#include "ArmorTrimChecks.hpp"
#include "ArmorTrimScene.hpp"
#include "ArmorTrimPreview.hpp"
#include "ToolboxPage.hpp"
#include "Theme.hpp"
#include "tools/ArmorTrimAssets.hpp"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFontDatabase>
#include <QCheckBox>
#include <cstdio>

int runArmorTrimChecks(QApplication &app) {
    const int fontId=QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts/msyh.ttc")));
    const auto families=QFontDatabase::applicationFontFamilies(fontId);
    if(!families.isEmpty()) app.setFont(QFont(families.first(),10));
    app.setStyle(Theme::createStyle()); Theme::setSpec(Theme::defaultSpec()); Theme::apply(app);
    int failures=0;
    const auto expect=[&](bool ok,const char *name) {
        std::printf("%s %s\n",ok?"PASS":"FAIL",name); if(!ok)++failures;
    };
    QHash<QString,QImage> images;
    QImage base(64,32,QImage::Format_ARGB32); base.fill(QColor(180,180,180));
    QImage trim(64,32,QImage::Format_ARGB32); trim.fill(QColor(1,2,3,128));
    QImage keys(8,1,QImage::Format_ARGB32); keys.fill(QColor(1,2,3));
    QImage palette(8,1,QImage::Format_ARGB32); palette.fill(QColor(30,220,90));
    QImage darker(8,1,QImage::Format_ARGB32); darker.fill(QColor(10,80,40));
    images.insert("models/armor/iron_layer_1",base); images.insert("models/armor/iron_layer_2",base);
    images.insert("trims/models/armor/coast",trim); images.insert("trims/models/armor/coast_leggings",trim);
    images.insert("trims/color_palettes/trim_palette",keys);
    images.insert("trims/color_palettes/iron",palette); images.insert("trims/color_palettes/iron_darker",darker);
    auto single=ArmorTrimScene::build(images,"iron",0,"coast","iron",false);
    expect(single.error.isEmpty() && single.scene.additionalParts.size()==1,"helmet composites base and trim on a double-sided shell");
    if(single.scene.additionalParts.size()==1) {
        const auto pixel=single.scene.additionalParts[0].texture.pixelColor(0,0);
        QImage expected=base; { QPainter painter(&expected); painter.fillRect(expected.rect(),QColor(10,80,40,128)); }
        expect(pixel==expected.pixelColor(0,0),"same material uses darker palette with correct alpha compositing");
        expect(single.scene.additionalParts[0].box.inflation==1.0f,"armor inflation does not modify UV dimensions");
    }
    const auto whole=ArmorTrimScene::build(images,"iron",0,"coast","iron",true);
    expect(whole.error.isEmpty() && whole.scene.additionalParts.size()==9,"whole set includes all four equipment slots");
    const auto legs=ArmorTrimScene::build(images,"iron",2,"coast","iron",false);
    expect(legs.error.isEmpty() && legs.scene.additionalParts.size()==3,"leggings include inner torso and both legs");
    if(legs.scene.additionalParts.size()==3)
        expect(legs.scene.additionalParts[0].box.inflation==.5f
               && legs.scene.additionalParts[2].box.mirror,"inner shell and left leg mirroring are explicit");
    images.insert("models/armor/turtle_layer_1",base);
    const auto turtleSet=ArmorTrimScene::build(images,"turtle",2,"coast","iron",true);
    expect(turtleSet.error.isEmpty() && turtleSet.scene.additionalParts.size()==1,"turtle whole set keeps helmet only");
    expect(!ArmorTrimScene::build(images,"iron",-1,"coast","iron",false).error.isEmpty(),"invalid slot rejected");
    expect(!ArmorTrimScene::build(images,"iron",0,"flow","iron",false).error.isEmpty(),"missing pattern never faked");
    auto wrong=images; wrong.insert("trims/color_palettes/iron_darker",base);
    expect(!ArmorTrimScene::build(wrong,"iron",0,"coast","iron",false).error.isEmpty(),"malformed palette rejected before indexing");
    for(double yaw:{0.0,90.0,180.0,270.0}) {
        auto scene=whole.scene; scene.yaw=yaw;
        QImage output(240,320,QImage::Format_ARGB32_Premultiplied); output.fill(Qt::transparent);
        QPainter painter(&output); SkinModel::render(painter,QRectF(0,0,240,320),scene); painter.end();
        int pixels=0;
        for(int y=0;y<output.height();++y) for(int x=0;x<output.width();++x) if(qAlpha(output.pixel(x,y)))++pixels;
        expect(pixels>1000,"textured shell renders from front/side/back without empty output");
    }
    const auto drawScene=[](const SkinModel::Scene &scene) {
        QImage output(240,320,QImage::Format_ARGB32_Premultiplied); output.fill(Qt::transparent);
        QPainter painter(&output); SkinModel::render(painter,QRectF(0,0,240,320),scene); return output;
    };
    SkinModel::Scene hollow;
    hollow.skin=QImage(64,64,QImage::Format_ARGB32); hollow.skin.fill(Qt::transparent);
    auto back=SkinModel::playerCuboids(false).first(); back.hasOverlay=false;
    QImage backTexture(64,32,QImage::Format_ARGB32); backTexture.fill(Qt::transparent);
    { QPainter painter(&backTexture); painter.fillRect(SkinModel::faceRect(back.base,8,8,8,SkinModel::Face::Back),Qt::red); }
    hollow.additionalParts.append({back,backTexture});
    QImage empty(240,320,QImage::Format_ARGB32_Premultiplied); empty.fill(Qt::transparent);
    expect(drawScene(hollow)==empty,"ordinary outward-only back face is culled from front");
    hollow.additionalParts[0].box.doubleSided=true;
    expect(drawScene(hollow)!=empty,"hollow armor back texture is visible through front opening");
    QImage player(64,64,QImage::Format_ARGB32); player.fill(QColor(75,145,190));
    const auto mixed=ArmorTrimScene::buildEquipment(images,{{"iron","coast","iron"},{"iron","",""},
                                                     {"iron","coast","iron"},{"","",""}},player,true);
    expect(mixed.error.isEmpty() && mixed.scene.additionalParts.size()==7 && mixed.scene.skin==player && mixed.scene.slim,
           "mixed slots include untrimmed armor and preserve the selected slim player skin");
    bool allDouble=true; for(const auto &part:mixed.scene.additionalParts) allDouble &= part.box.doubleSided;
    expect(allDouble,"adding player skin does not disable double-sided armor");
    SkinModel::Scene first=single.scene;
    first.additionalParts=first.additionalParts.mid(0,1);
    auto equalPlane=first;
    auto duplicate=first.additionalParts.first(); duplicate.texture.fill(QColor(250,10,10));
    equalPlane.additionalParts.append(duplicate);
    expect(drawScene(first)==drawScene(equalPlane),"equal-depth faces consistently keep first writer without speckles");
    const QString fixtureRoot=QDir::currentPath()+QStringLiteral("/build");
    expect(QDir().mkpath(fixtureRoot),"writable fixture root exists");
    QTemporaryDir temporary(fixtureRoot+QStringLiteral("/trim-test-XXXXXX"));
    QFile broken(temporary.filePath("broken.jar"));
    const bool created=temporary.isValid() && broken.open(QIODevice::WriteOnly);
    expect(created,"corrupt archive fixture is actually created");
    if(created) { broken.write("not a jar"); broken.close(); }
    expect(!ArmorTrimAssets::load(broken.fileName()).error.isEmpty(),"corrupt archive rejected without extraction");
    ToolboxPage page; page.showTool(ToolboxPage::Tool::Trim);
    page.setTrimArmorMaterials({"皮革","锁链","铁","金","钻石","下界合金","海龟壳"});
    page.setTrimParts({"头盔","胸甲","护腿","靴子"});
    auto *armorCombo=page.findChild<QComboBox *>(ToolboxPage::trimArmorComboName());
    auto *partCombo=page.findChild<QComboBox *>(ToolboxPage::trimPartComboName());
    auto *wholeCheck=page.findChild<QCheckBox *>(ToolboxPage::trimWholeSetName());
    partCombo->setCurrentIndex(2); wholeCheck->setChecked(true); armorCombo->setCurrentIndex(6);
    expect(wholeCheck->isChecked() && partCombo->currentIndex()==0 && !partCombo->isEnabled(),"turtle keeps whole-set choice but locks unsupported slots");
    armorCombo->setCurrentIndex(2); wholeCheck->setChecked(false);
    expect(partCombo->isEnabled(),"switching back to ordinary armor restores part selection");
    page.setTrimPatterns({"海岸","哨兵"}); page.setTrimMaterials({"铁","黄金"});
    auto *slot0=page.findChild<QComboBox *>("trimArmorSlot0");
    auto *slot1=page.findChild<QComboBox *>("trimArmorSlot1");
    auto *pattern1=page.findChild<QComboBox *>("trimPatternSlot1");
    slot0->setCurrentIndex(slot0->findData(6)); slot1->setCurrentIndex(slot1->findData(2));
    pattern1->setCurrentIndex(pattern1->findData(-1));
    expect(page.trimEquipment()[0].armor==6 && page.trimEquipment()[1].armor==2
           && page.trimEquipment()[1].pattern==-1,"four columns maintain independent armor and trim choices");
    expect(!page.findChild<QComboBox *>("trimMaterialSlot1")->isEnabled(),"untrimmed armor disables irrelevant material selector");
    slot1->setCurrentIndex(slot1->findData(6));
    expect(page.trimEquipment()[1].armor==-1,"turtle shell cannot occupy chest slot even on programmatic restore");
    page.setTrimArmorMaterials({QStringLiteral("铁")}); page.setTrimParts({QStringLiteral("头盔")});
    page.setTrimPatterns({QStringLiteral("海岸")}); page.setTrimMaterials({QStringLiteral("铁")});
    int action=-1; QObject::connect(&page,&ToolboxPage::trimResourcesRequested,&page,[&](int value){action=value;});
    for(int i=0;i<3;++i) {
        auto *button=page.findChild<QPushButton *>(QStringLiteral("trimResourceAction%1").arg(i));
        expect(button!=nullptr,"resource action button exists"); if(button) { button->click(); expect(action==i,"resource action reaches controller signal"); }
    }
    page.setTrimResourceStatus(QStringLiteral("下载中"),true);
    expect(!page.findChild<QPushButton *>("trimResourceAction2")->isEnabled()
           && page.findChild<QPushButton *>("trimResourceAction3")->isEnabled(),"busy state blocks duplicate downloads and enables cancel");
    page.setTrimResourceStatus(QStringLiteral("就绪"));
    // 披风是选修项：默认不勾；收到账号外观前那一格是灰的，有了披风也不替用户勾上。
    auto *capeCheck=page.findChild<QCheckBox *>(ToolboxPage::trimCapeCheckName());
    expect(capeCheck!=nullptr && !capeCheck->isChecked() && !capeCheck->isEnabled(),
           "cape checkbox starts unchecked and disabled before an account cape arrives");
    bool capeToggled=false;
    QObject::connect(&page,&ToolboxPage::trimCapeVisibilityChanged,&page,
                     [&](bool visible){capeToggled=visible;});
    page.setTrimCapeAvailable(true);
    expect(capeCheck->isEnabled() && !capeCheck->isChecked() && !page.trimShowCape(),
           "an available cape enables the checkbox without auto-checking it");
    capeCheck->setChecked(true);
    expect(capeToggled && page.trimShowCape(),"checking the box turns the cape on");
    page.setTrimCapeAvailable(false);
    expect(!capeCheck->isChecked() && !page.trimShowCape() && !capeCheck->isEnabled(),
           "losing the cape unchecks and disables the box without a fake cape");
    QImage cape(64,32,QImage::Format_ARGB32); cape.fill(QColor(200,40,40));
    const auto withCape=ArmorTrimScene::buildEquipment(images,{{"iron","coast","iron"}},player,false,cape);
    const auto withoutCape=ArmorTrimScene::buildEquipment(images,{{"iron","coast","iron"}},player,false);
    expect(withCape.scene.cape==cape && withoutCape.scene.cape.isNull(),
           "cape reaches the scene only when the caller supplies it");
    {
        // 光有图不算数：从背面看过去，披风那一块真被画出来（和没有披风那一帧有差）。
        SkinModel::Scene bare; bare.skin=player; bare.yaw=180;
        SkinModel::Scene dressed=bare; dressed.cape=cape;
        const auto bareFrame=drawScene(bare), capeFrame=drawScene(dressed);
        int redPixels=0;
        for(int y=0;y<capeFrame.height();++y) for(int x=0;x<capeFrame.width();++x) {
            const auto c=capeFrame.pixelColor(x,y);
            if(c.alpha()>0 && c.red()>150 && c.green()<80 && c.blue()<80) ++redPixels;
        }
        expect(capeFrame!=bareFrame && redPixels>100,"a supplied cape is actually rasterized on the model");
    }
    {
        // 拖拽方向：往右拖 = 抓住模型拧，正面往右滑 => yaw 减（和 SkinViewer 同一条规矩）。
        ArmorTrimPreview dragPreview; dragPreview.resize(240,320);
        auto dragScene=whole.scene; dragScene.yaw=0; dragScene.pitch=0; dragPreview.setScene(dragScene);
        const double yawBefore=dragPreview.scene().yaw;
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(50,50),QPointF(50,50),
                          Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove,QPointF(90,50),QPointF(90,50),
                         Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&dragPreview,&press);
        QCoreApplication::sendEvent(&dragPreview,&move);
        expect(dragPreview.scene().yaw<yawBefore,
               "dragging right rotates the model toward its own right side (yaw decreases)");
    }
    const QString archive=qEnvironmentVariable("WTFML_TRIM_TEST_JAR");
    if(!archive.isEmpty()) {
        const auto actual=ArmorTrimAssets::load(archive);
        expect(actual.error.isEmpty(),"actual user-provided client archive loads");
        const auto actualScene=ArmorTrimScene::build(actual.images,"diamond",0,"silence","amethyst",true);
        expect(actualScene.error.isEmpty() && actualScene.scene.additionalParts.size()==9,"actual textures compose full trimmed armor");
        for(const auto &armor:QStringList{"leather","chainmail","iron","gold","diamond","netherite","turtle"})
            expect(ArmorTrimScene::build(actual.images,armor,0,"coast","gold",armor!="turtle").error.isEmpty(),
                   "actual armor material textures compose without missing layers");
        for(const auto &pattern:QStringList{"coast","sentry","dune","wild","ward","eye","vex","tide","snout",
                                           "rib","spire","wayfinder","shaper","silence","raiser","host","flow","bolt"})
            expect(ArmorTrimScene::build(actual.images,"diamond",0,pattern,"diamond",true).error.isEmpty(),
                   "actual pattern includes outer and leggings textures and same-material palette");
        // 树脂质是 1.21.4 才有的：老的客户端里本来就没有它，不拿它判老包的完整性。
        QStringList materials{"amethyst","copper","diamond","emerald","gold","iron","lapis","netherite","quartz","redstone"};
        if(actual.images.contains(QStringLiteral("trims/color_palettes/resin")))
            materials.append(QStringLiteral("resin"));
        for(const auto &material:materials)
            expect(ArmorTrimScene::build(actual.images,"iron",0,"coast",material,true).error.isEmpty(),
                   "actual trim material palette composes");
        page.setTrimArmorMaterials({QStringLiteral("钻石")}); page.setTrimPatterns({QStringLiteral("静默")});
        page.setTrimMaterials({QStringLiteral("紫水晶")});
        for(int slot=0;slot<4;++slot) {
            auto *armor=page.findChild<QComboBox *>(QStringLiteral("trimArmorSlot%1").arg(slot)); armor->setCurrentIndex(armor->findData(0));
            auto *pattern=page.findChild<QComboBox *>(QStringLiteral("trimPatternSlot%1").arg(slot)); pattern->setCurrentIndex(pattern->findData(0));
        }
        page.restoreTrimEquipment(true);
        page.setTrimResult(QStringLiteral("钻石头盔：静默 · 紫水晶\n钻石胸甲：静默 · 紫水晶\n钻石护腿：静默 · 紫水晶\n钻石靴子：静默 · 紫水晶\n\n材料：静默纹饰模板 × 4 · 紫水晶碎片 × 4\n钻石头盔 / 胸甲 / 护腿 / 靴子各一件\n\n模板来源：静默：远古城市"));
        page.setTrimResourceStatus(QStringLiteral("资源：1.21.1 · 拖动旋转，滚轮缩放"));
        const auto dressed=ArmorTrimScene::buildEquipment(actual.images,{{"diamond","silence","amethyst"},
            {"diamond","silence","amethyst"},{"diamond","silence","amethyst"},{"diamond","silence","amethyst"}},player,false);
        page.setTrimPreview(dressed.scene);
        expect(static_cast<ArmorTrimPreview *>(page.findChild<QWidget *>("armorTrimPreview"))->scene().skin==player,
               "preview receives player skin alongside independently built equipment");
        const QString preview=qEnvironmentVariable("WTFML_TRIM_PREVIEW");
        if(!preview.isEmpty()) {
            page.resize(1080,580); page.show(); app.processEvents();
            expect(page.grab().save(preview),"save runtime resource preview screenshot");
        }
    } else page.setTrimPreview(whole.scene);
    for(const QSize size:{QSize(900,560),QSize(1200,700)}) {
        page.resize(size); page.show(); app.processEvents();
        auto *preview=page.findChild<QWidget *>("armorTrimPreview");
        expect(preview && preview->isVisible() && preview->width()>=200 && preview->height()>=220 && preview->height()<=360,"preview stays compact at supported window sizes");
        auto *a=page.findChild<QComboBox *>("trimArmorSlot0"); auto *b=page.findChild<QComboBox *>("trimArmorSlot3");
        expect(a->mapTo(&page,QPoint()).y()==b->mapTo(&page,QPoint()).y() && a->width()>=70,
               "all four armor columns stay aligned and usable without wrapping");
    }
    return failures?1:0;
}

