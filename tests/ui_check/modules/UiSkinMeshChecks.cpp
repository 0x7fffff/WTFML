#include "Support.hpp"
int UiSkinMeshChecks() {
    {
        using SkinModel::Face;
        const auto rt = [](const QRect &r) {
            return QStringLiteral("%1,%2 %3x%4")
                .arg(r.x())
                .arg(r.y())
                .arg(r.width())
                .arg(r.height());
        };

        // ---- W1 贴图布局公式 ----
        // 这是整套渲染里最没有把握的一处：写反了的表现是**脸左右镜像**，而那种错误
        // 在 260 px 宽的栏里基本看不出来。所以拿两套互不相干的公认布局交叉验证 ——
        // 头（皮肤图）和披风（另一张 64×32 的图）。两个都对上才敢用这条公式。
        const QRect headFront = SkinModel::faceRect(QPoint(0, 0), 8, 8, 8, Face::Front);
        check(headFront == QRect(8, 8, 8, 8), "W1 头的前脸在 (8,8,8,8)", rt(headFront));
        const QRect hatFront = SkinModel::faceRect(QPoint(32, 0), 8, 8, 8, Face::Front);
        check(hatFront == QRect(40, 8, 8, 8), "W1 帽子层的前脸在 (40,8,8,8)", rt(hatFront));
        const QRect bodyFront = SkinModel::faceRect(QPoint(16, 16), 8, 12, 4, Face::Front);
        check(bodyFront == QRect(20, 20, 8, 12), "W1 身体正面在 (20,20,8,12)", rt(bodyFront));
        const QRect capeFront = SkinModel::faceRect(QPoint(0, 0), 10, 16, 1, Face::Front);
        const QRect capeBack = SkinModel::faceRect(QPoint(0, 0), 10, 16, 1, Face::Back);
        check(capeFront == QRect(1, 1, 10, 16) && capeBack == QRect(12, 1, 10, 16),
              "W1 披风正/背面在 (1,1,10,16) / (12,1,10,16) —— 这是**另一张**图，它也对上"
              "就说明公式不是照着皮肤那张凑出来的",
              rt(capeFront) + QStringLiteral(" / ") + rt(capeBack));
        const QRect hatBack = SkinModel::faceRect(QPoint(32, 0), 8, 8, 8, Face::Back);
        check(hatBack.x() + hatBack.width() == 64,
              "W1 帽子层的背面正好贴到贴图右边缘（32..64 那半边就是为它留的）", rt(hatBack));

        // ---- W2 六个面都不会镜像，也不会拉伸 ----
        const auto boxNamed = [](const QVector<SkinModel::Cuboid> &boxes, const QString &name) {
            for (const SkinModel::Cuboid &box : boxes) {
                if (box.name == name) {
                    return box;
                }
            }
            return SkinModel::Cuboid();
        };
        QVector<SkinModel::Cuboid> allBoxes = SkinModel::playerCuboids(false);
        allBoxes << SkinModel::capeCuboid();
        int mirrored = 0;
        int stretched = 0;
        QString firstBad;
        for (const SkinModel::Cuboid &box : allBoxes) {
            for (Face face : SkinModel::allFaces()) {
                for (int layer = 0; layer < 2; ++layer) {
                    const bool overlay = layer == 1;
                    if (overlay && !box.hasOverlay) {
                        continue;
                    }
                    const SkinModel::FaceGeometry g = SkinModel::faceGeometry(box, face, overlay);
                    // 贴图的"往右"和"往下"在模型空间里是哪两个方向：由四个角反推
                    // （而不是再问一遍那张基向量表 —— 那等于拿自己验自己）。
                    const QVector3D right = (g.corners.at(1) - g.corners.at(0)).normalized();
                    const QVector3D up = (g.corners.at(0) - g.corners.at(3)).normalized();
                    if (QVector3D::crossProduct(right, up).distanceToPoint(g.normal) > 0.001f) {
                        ++mirrored;
                        firstBad = box.name + SkinModel::faceName(face);
                    }
                    // 基础层是**一个模型单位对一个贴图像素**：四个角撑开的长度必须正好
                    // 等于贴图那块矩形的宽高。覆盖层不参加 —— 它是把同一块贴图摊在胀大
                    // 0.5 的面上（游戏就是这么做的）。
                    if (!overlay) {
                        const double spanRight = (g.corners.at(1) - g.corners.at(0)).length();
                        const double spanUp = (g.corners.at(0) - g.corners.at(3)).length();
                        if (qAbs(spanRight - g.uv.width()) > 0.001 ||
                            qAbs(spanUp - g.uv.height()) > 0.001) {
                            ++stretched;
                            firstBad = box.name + SkinModel::faceName(face);
                        }
                    }
                }
            }
        }
        check(mirrored == 0,
              "W2 六个面（基础层 + 覆盖层 + 披风）都满足 right × up == normal —— 贴图不会镜像",
              firstBad);
        check(stretched == 0, "W2 基础层一个贴图像素对一个模型单位（贴图没被拉伸）", firstBad);

        const QVector<SkinModel::Cuboid> parts = SkinModel::playerCuboids(false);
        // 顺序是**对外承诺**的：walkingParts() 按下标往对应部件上填关节角度，改了顺序
        // 就会"腿的角度填到手臂上"—— 那种错在 260 px 的缩略图上根本看不出来。
        {
            QStringList order;
            for (const SkinModel::Cuboid &box : parts) {
                order << box.name;
            }
            const QStringList expected = {
                QStringLiteral("头"), QStringLiteral("身体"), QStringLiteral("右臂"),
                QStringLiteral("左臂"), QStringLiteral("右腿"), QStringLiteral("左腿")};
            check(order == expected,
                  "W2 部件表的顺序固定：头、身体、右臂、左臂、右腿、左腿"
                  "（walkingParts 按下标往对应部件上填角度）",
                  order.join(QStringLiteral(",")));
        }
        double feetY = 1e9;
        double topY = -1e9;
        for (const SkinModel::Cuboid &box : parts) {
            feetY = qMin(feetY, static_cast<double>(box.lo.y()));
            topY = qMax(topY, static_cast<double>(box.hi.y()));
        }
        check(feetY == 0.0 && topY == 32.0,
              "W2 部件表：脚底 y=0、头顶 y=32（整个小人 32 单位高）",
              QString("%1..%2").arg(feetY).arg(topY));
        const SkinModel::Cuboid rArm = boxNamed(parts, QStringLiteral("右臂"));
        const SkinModel::Cuboid lArm = boxNamed(parts, QStringLiteral("左臂"));
        check(rArm.lo.x() == -8 && rArm.hi.x() == -4 && lArm.lo.x() == 4 && lArm.hi.x() == 8,
              "W2 两条 4 宽的手臂挂在身体两侧（右臂 -x、左臂 +x）",
              QString("%1 / %2").arg(rArm.lo.x()).arg(lArm.lo.x()));
        const SkinModel::Cuboid slimArm =
            boxNamed(SkinModel::playerCuboids(true), QStringLiteral("右臂"));
        check(slimArm.hi.x() == -4 && slimArm.lo.x() == -7,
              "W2 纤细模型的手臂 3 宽：贴着身体那一侧不动（不然会跟身体分家），外侧往里让一格",
              QString("%1..%2").arg(slimArm.lo.x()).arg(slimArm.hi.x()));
        const SkinModel::Cuboid cape = SkinModel::capeCuboid();
        check(cape.lo.z() < 0 && cape.hi.z() < 0,
              "W2 披风在 -z（背后那一侧）—— 玩家面朝 +z，挂到 +z 就成了胸前的围兜",
              QString("z %1..%2").arg(cape.lo.z()).arg(cape.hi.z()));

        const SkinModel::Cuboid rLeg = boxNamed(parts, QStringLiteral("右腿"));
        const SkinModel::Cuboid lLeg = boxNamed(parts, QStringLiteral("左腿"));
        check(qAbs(rArm.pivot.y() - 24.0) < 0.001 && qAbs(rArm.pivot.x() + 6.0) < 0.001 &&
                  qAbs(lArm.pivot.y() - 24.0) < 0.001 && qAbs(lArm.pivot.x() - 6.0) < 0.001,
              "W2 手臂的关节在肩膀上（y=24、x 取手臂中线 ±6 —— 右边是负的，跟部件一样）",
              QString("右 %1,%2 / 左 %3,%4")
                  .arg(rArm.pivot.x())
                  .arg(rArm.pivot.y())
                  .arg(lArm.pivot.x())
                  .arg(lArm.pivot.y()));
        check(qAbs(rLeg.pivot.y() - 12.0) < 0.001 && qAbs(lLeg.pivot.y() - 12.0) < 0.001 &&
                  qAbs(rLeg.pivot.x() + 2.0) < 0.001 && qAbs(lLeg.pivot.x() - 2.0) < 0.001,
              "W2 腿的关节在胯上（y=12、x 取腿的中线 ±2）",
              QString("右 %1,%2 / 左 %3,%4")
                  .arg(rLeg.pivot.x())
                  .arg(rLeg.pivot.y())
                  .arg(lLeg.pivot.x())
                  .arg(lLeg.pivot.y()));
        check(qAbs(slimArm.pivot.x() + 5.5) < 0.001,
              "W2 纤细模型的手臂关节跟着让到 ±5.5 —— 说明它是从 lo/hi 现算的，不是抄死的数字",
              QString::number(slimArm.pivot.x()));

        // **披风朝外那一面取贴图上哪一块**：这条钉的是踩过的那个 bug。
        //
        // 从背后看，看见的必须是披风图左边那块 (1,1,10,16)。原来的写法是"背面那一面取
        // 背面那块图"（(12,1,10,16)），于是从背后看见的是披风图的**内侧** —— 整件披风
        // 是反的（用户报的"披风放反了"）。这里不钉"转了多少度"这个做法，钉的是结果：
        // **法线指着 -z（背对身体的）那一面，取图必须是左边那块**。
        //
        // NOTE: 披风转过 180°，所以它**材质**上的"正面"（Face::Front）现在朝着 -z 那侧 ——
        // 下面这两行读起来有点绕（Front 面法线朝外），但它就是这么回事。
        {
            const SkinModel::FaceGeometry outward = SkinModel::faceGeometry(cape, Face::Front);
            const SkinModel::FaceGeometry inward = SkinModel::faceGeometry(cape, Face::Back);
            check(outward.normal.z() < -0.9f && outward.uv == QRect(1, 1, 10, 16),
                  "W2 披风朝外那面（法线 -z）取贴图左边那块 (1,1,10,16) ——"
                  "取成 (12,1,10,16) 的话从背后看见的是披风的内侧",
                  QString("法线 z %1、取图 %2").arg(outward.normal.z()).arg(rt(outward.uv)));
            check(inward.normal.z() > 0.9f && inward.uv == QRect(12, 1, 10, 16),
                  "W2 披风贴身体那面（法线 +z）取右边那块 (12,1,10,16)",
                  QString("法线 z %1、取图 %2").arg(inward.normal.z()).arg(rt(inward.uv)));

            // 光钉"取哪一块矩形"还不够：**矩形贴在面上的方向**是另一件事。贴图左边（u=0）
            // 那一条边必须落在玩家自己的左边（+x）。从背后看，玩家的左边正好是看的人的左边
            // —— 所以披风图上的图案（有字的、不居中的）不会被左右翻过来。
            // 上面那条 right × up == normal 管的是"六个面互相之间不许镜像"，它管不了
            // "这件披风整体被左右翻了个个儿"（整块翻过去，六个面照样条条成立）。
            check(outward.corners.at(0).x() > 4.9f && outward.corners.at(1).x() < -4.9f &&
                      outward.corners.at(0).y() > 23.9f,
                  "W2 披风朝外那面：贴图的左边(u=0)、上边(v=0)贴在玩家的左上 —— 图案不翻",
                  QString("u=0 那个角 (%1, %2)")
                      .arg(outward.corners.at(0).x(), 0, 'f', 2)
                      .arg(outward.corners.at(0).y(), 0, 'f', 2));
        }

        // ---- W3 相机：哪个面看得见、模型在画面里多大 ----
        const QRectF target(0, 0, 180, 240);
        const SkinModel::Cuboid body = boxNamed(parts, QStringLiteral("身体"));
        const auto sees = [&target, &body](double yaw, Face face) {
            return SkinModel::isFaceVisible(SkinModel::makeCamera(target, yaw, 0.0, 1.0),
                                            SkinModel::faceGeometry(body, face));
        };
        check(sees(0, Face::Front) && !sees(0, Face::Back),
              "W3 yaw=0 正对着脸：看得见正面、背面被剔掉");
        check(sees(90, Face::Left) && !sees(90, Face::Right),
              "W3 yaw=90 绕到一边：+x 那一侧看得见（那是**玩家自己的左边**）");
        check(sees(180, Face::Back) && !sees(180, Face::Front),
              "W3 yaw=180 转过去：背面朝镜头、正面被剔掉");
        check(sees(-90, Face::Right) && !sees(-90, Face::Left), "W3 yaw=-90 绕到另一边");

        const double defYaw = SkinViewer::defaultYaw();
        const double defPitch = SkinViewer::defaultPitch();
        const double restPhase = SkinModel::restWalkPhase();
        const QRectF bounds =
            SkinModel::projectedBounds(target, defYaw, defPitch, 1.0, false, true, restPhase);
        const double ratio = bounds.height() / target.height();
        check(ratio > 0.7 && ratio < 0.95,
              "W3 默认视角下小人占画面高度的七到九成（焦距按控件高度现算）",
              QString::number(ratio, 'f', 3));
        // 摊开的轮廓得**整个**落在画面里：焦距是按**高度**算的，而披风翘起来（24°）之后
        // 横向会长出一截 —— 那是唯一会把画面撑出去的方向。撑出去的话用户看见的是"披风被
        // 切掉一块"，可上面那条按高度算的比例还是好好的、一点看不出问题。
        check(bounds.width() <= target.width() && bounds.height() <= target.height(),
              "W3 默认视角下整只小人（含翘起来的披风）都在画面里，没有被切掉",
              QString("轮廓 %1x%2 / 画面 %3x%4")
                  .arg(bounds.width(), 0, 'f', 1)
                  .arg(bounds.height(), 0, 'f', 1)
                  .arg(target.width())
                  .arg(target.height()));
        const QRectF bigTarget(0, 0, 360, 480);
        const double bigRatio = SkinModel::projectedBounds(bigTarget, defYaw, defPitch, 1.0, false,
                                                           true, restPhase)
                                    .height() /
                                bigTarget.height();
        check(qAbs(bigRatio - ratio) < 0.01,
              "W3 控件大一倍，占的比例不变 —— 这就是\"自适应缩放\"",
              QString("%1 vs %2").arg(ratio, 0, 'f', 3).arg(bigRatio, 0, 'f', 3));
        const double doubled =
            SkinModel::projectedBounds(target, defYaw, defPitch, 2.0, false, true, restPhase)
                .height();
        check(qAbs(doubled - bounds.height() * 2.0) < 0.5,
              "W3 zoom 是乘在焦距上的：zoom=2 投影正好大一倍",
              QString("%1 vs %2").arg(doubled).arg(bounds.height() * 2.0));
        // 姿势真的进了投影：**侧着看**时，走路那几帧的轮廓明显更宽 —— 腿一前一后摆出去，
        // 立正时那点厚度就只有身体的 4 个单位。横向跨度比高度更能说明这件事。
        const double restSide =
            SkinModel::projectedBounds(target, 90.0, 0.0, 1.0, false, false, restPhase).width();
        const double walkSide =
            SkinModel::projectedBounds(target, 90.0, 0.0, 1.0, false, false, 0.0).width();
        check(walkSide > restSide * 1.3,
              "W3 侧面看：迈步那一帧比立正宽得多（腿摆到前后两边去了）——"
              "说明走路姿势真的传到了投影上",
              QString("立正 %1 vs 迈步 %2").arg(restSide, 0, 'f', 1).arg(walkSide, 0, 'f', 1));

        // ---- W4 头像：从皮肤上把头抠下来 ----
        const QColor faceRed(220, 40, 40);
        const QColor hatGreen(40, 200, 60);
        const QColor moleBlue(40, 60, 220);
        QImage avatarSrc(64, 64, QImage::Format_ARGB32_Premultiplied);
        avatarSrc.fill(Qt::transparent);
        {
            QPainter p(&avatarSrc);
            p.fillRect(SkinModel::faceRect(QPoint(0, 0), 8, 8, 8, Face::Front), faceRed);
            // 脸上一颗**单独**的痣（贴图第 3 列、第 0 行）：拿它验放大是最近邻 ——
            // 插值放大会把它和旁边的红混出一个中间色，边界也就跟着糊了。
            p.fillRect(QRect(11, 8, 1, 1), moleBlue);
            // 帽子层的左上角那一格。
            p.fillRect(QRect(40, 8, 1, 1), hatGreen);
        }
        const QImage avatar = SkinModel::headAvatar(avatarSrc, 16);
        check(avatar.size() == QSize(16, 16), "W4 头像是正方形的 16×16",
              QString("%1x%2").arg(avatar.width()).arg(avatar.height()));
        check(avatar.pixelColor(0, 0) == hatGreen && avatar.pixelColor(1, 1) == hatGreen,
              "W4 帽子层叠在基础层上面（头发比脸更靠外，得盖住脸）");
        check(avatar.pixelColor(2, 0) == faceRed, "W4 帽子层没画到的地方露出脸");
        check(avatar.pixelColor(6, 0) == moleBlue && avatar.pixelColor(4, 0) == faceRed,
              "W4 放大是最近邻：8×8 一个像素正好铺成 2×2 一块（(6,0) 是那颗痣、(4,0) 已经是隔壁）",
              QString("%1 / %2")
                  .arg(avatar.pixelColor(6, 0).name(), avatar.pixelColor(4, 0).name()));
        QImage legacy(64, 32, QImage::Format_ARGB32_Premultiplied);
        legacy.fill(Qt::transparent);
        {
            QPainter p(&legacy);
            p.fillRect(SkinModel::faceRect(QPoint(0, 0), 8, 8, 8, Face::Front), faceRed);
            p.fillRect(QRect(40, 8, 1, 1), hatGreen);
        }
        check(SkinModel::headAvatar(legacy, 8).pixelColor(0, 0) == hatGreen,
              "W4 老格式（64×32）的帽子层照样裁 —— 那一层那个年代就有，多出来的右半边就是为它留的");
        check(SkinModel::headAvatar(QImage(), 16).isNull() &&
                  SkinModel::headAvatar(QImage(8, 8, QImage::Format_ARGB32), 16).isNull(),
              "W4 贴图不合法（空 / 太小）时返回空图，不去越界取像素");

        // ---- W4b 兜底那张占位皮肤：整身 #000000 + 脸上一枚 #ffffff 的问号 ----
        //
        // 用户 的原话就是这两个色号（「整个人都是 #000000，脸部一个 #ffffff 的
        // 问号」），所以这儿量的是**逐格等于**，不是"深色/浅色"那种模糊判据 —— 配色一动
        // 就该红。定位那一格用的是 `SkinModel::faceRect()`（和渲染那边**同一个出处**），
        // 所以我写的是"脸上那一格长什么样"，不是"贴图 (8,8) 起那 8×8 长什么样"。
        {
            const QImage defSkin = SkinModel::defaultSteveSkin();
            check(defSkin.size() == QSize(64, 64), "W4b 兜底皮肤是 64×64 的新格式贴图",
                  QString("%1x%2").arg(defSkin.width()).arg(defSkin.height()));

            const QColor black(0x00, 0x00, 0x00);
            const QColor white(0xFF, 0xFF, 0xFF);
            const QRect face = SkinModel::faceRect(QPoint(0, 0), 8, 8, 8, Face::Front);

            // 全图只许有这三种像素：透明的、不透明的纯黑、不透明的纯白。多出第四种就说明
            // 有人往里加了别的东西 —— 这条管的是"颜色由我们自己决定"那一格。
            int foreign = 0;
            for (int y = 0; y < defSkin.height(); ++y) {
                for (int x = 0; x < defSkin.width(); ++x) {
                    const QColor c = defSkin.pixelColor(x, y);
                    if (c.alpha() == 0) {
                        continue;
                    }
                    if (c.alpha() != 255 || (c != black && c != white)) {
                        ++foreign;
                    }
                }
            }
            check(foreign == 0,
                  "W4b 全图只有三种像素：透明 / 不透明的 #000000 / 不透明的 #ffffff",
                  QString("%1 格是第四种").arg(foreign));

            // 脸上那一格**逐格**对一遍：写死的期望值，`.` 是黑的底、`#` 是白的问号，
            // `?` 是"既不是黑也不是白"（拿到它就说明那一格是透的或者混色了）。
            // 一行 8 个字符、共 8 行 —— 那一格就是 8×8。4×6 的字形摆在 (2,1)：
            // 左右各留 2 格、上下各留 1 格（摆位见 defaultSteveSkin()）。
            const char *const kExpectedFace[8] = {
                "........",
                "...##...",
                "..#..#..",
                ".....#..",
                "....#...",
                "........",
                "....#...",
                "........",
            };
            QString faceGot;
            QString faceWant;
            int faceBad = 0;
            for (int row = 0; row < 8; ++row) {
                for (int col = 0; col < 8; ++col) {
                    const QColor c = defSkin.pixelColor(face.x() + col, face.y() + row);
                    const char got = (c == white) ? '#' : (c == black ? '.' : '?');
                    faceGot += QLatin1Char(got);
                    faceWant += QLatin1Char(kExpectedFace[row][col]);
                    if (got != kExpectedFace[row][col]) {
                        ++faceBad;
                    }
                }
            }
            check(faceBad == 0, "W4b 脸上那一格就是黑底 + 一枚白问号（逐格比）",
                  QString("%1 格不对 —— 拿到 %2 / 应该是 %3").arg(faceBad).arg(faceGot, faceWant));

            // 用户 又提了一句「那个问号能不能再居中一点点，就算小一点也没关系」——
            // 于是**居中也得钉住**，不然下次谁把字形挪回去（或者挪过头半个格）就没人拦。
            // 判据不是"我记得偏移是 (2,1)"，而是**拿那一格上的白格子算出来的包围盒**：
            // 左右余量相等、上下余量相等。这比写死偏移硬 —— 换一个字形尺寸（只要是偶数宽）
            // 这条断言照样成立。
            int minX = 8, maxX = -1, minY = 8, maxY = -1;
            for (int row = 0; row < 8; ++row) {
                for (int col = 0; col < 8; ++col) {
                    if (defSkin.pixelColor(face.x() + col, face.y() + row) != white) {
                        continue;
                    }
                    minX = qMin(minX, col);
                    maxX = qMax(maxX, col);
                    minY = qMin(minY, row);
                    maxY = qMax(maxY, row);
                }
            }
            check(minX >= 0 && maxX >= 0,
                  "W4b 现状：脸上确实有白的（不然下面那条居中断言在空脸上也绿）",
                  QString("x %1..%2 / y %3..%4").arg(minX).arg(maxX).arg(minY).arg(maxY));
            check(minX == 8 - 1 - maxX && minY == 8 - 1 - maxY,
                  "W4b 那枚问号是**正中**的（左右余量相等、上下余量相等）",
                  QString("左 %1 右 %2 / 上 %3 下 %4")
                      .arg(minX)
                      .arg(8 - 1 - maxX)
                      .arg(minY)
                      .arg(8 - 1 - maxY));

            // 里层那六块（脑袋 / 身体 / 两臂 / 两腿）**除了脸上那一格**：每一格都必须是不
            // 透明的纯黑。少刷一块的症状是那一面**整块不画**（贴图透明 = 没有像素），
            // 小人身上就缺一块 —— 而缺一块在屏幕上不会报任何错。
            struct SkinPart {
                QPoint base;
                int w, h, d;
            };
            const SkinPart kParts[6] = {
                {QPoint(0, 0), 8, 8, 8},     // 脑袋
                {QPoint(16, 16), 8, 12, 4},  // 身体
                {QPoint(40, 16), 4, 12, 4},  // 右臂
                {QPoint(32, 48), 4, 12, 4},  // 左臂
                {QPoint(0, 16), 4, 12, 4},   // 右腿
                {QPoint(16, 48), 4, 12, 4},  // 左腿
            };
            int bodyBad = 0;
            int bodyCells = 0;
            for (const SkinPart &part : kParts) {
                for (Face f : SkinModel::allFaces()) {
                    const QRect r =
                        SkinModel::faceRect(part.base, part.w, part.h, part.d, f);
                    if (r == face) {
                        continue;  // 脸上有那枚问号，上面单独量过
                    }
                    for (int y = r.top(); y <= r.bottom(); ++y) {
                        for (int x = r.left(); x <= r.right(); ++x) {
                            ++bodyCells;
                            const QColor c = defSkin.pixelColor(x, y);
                            if (c.alpha() != 255 || c != black) {
                                ++bodyBad;
                            }
                        }
                    }
                }
            }
            check(bodyCells > 0 && bodyBad == 0,
                  "W4b 除脸以外那二十五面每一格都是不透明的 #000000",
                  QString("%1 格不对（共 %2 格）").arg(bodyBad).arg(bodyCells));

            // 第二层（帽子 / 外套 / 两只袖子 / 两条裤脚）**一个格子都不许画**。这不是洁癖：
            // 那一层盖在里层外面（render() 里第二层那段），涂上颜色就会把脸上那枚问号整个
            // 挡住 —— 而"挡住了"在屏幕上看起来就是"问号没了"，同样不报错。
            const SkinPart kOverlays[6] = {
                {QPoint(32, 0), 8, 8, 8},    // 帽子
                {QPoint(16, 32), 8, 12, 4},  // 外套
                {QPoint(40, 32), 4, 12, 4},  // 右袖
                {QPoint(48, 48), 4, 12, 4},  // 左袖
                {QPoint(0, 32), 4, 12, 4},   // 右裤脚
                {QPoint(0, 48), 4, 12, 4},   // 左裤脚
            };
            int overlayPainted = 0;
            int overlayCells = 0;
            for (const SkinPart &part : kOverlays) {
                for (Face f : SkinModel::allFaces()) {
                    const QRect r =
                        SkinModel::faceRect(part.base, part.w, part.h, part.d, f);
                    for (int y = r.top(); y <= r.bottom(); ++y) {
                        for (int x = r.left(); x <= r.right(); ++x) {
                            ++overlayCells;
                            if (defSkin.pixelColor(x, y).alpha() != 0) {
                                ++overlayPainted;
                            }
                        }
                    }
                }
            }
            check(overlayCells > 0 && overlayPainted == 0,
                  "W4b 第二层（帽子/外套/袖子/裤脚）全程透明 —— 涂了就把脸上的问号挡住了",
                  QString("%1 格被画上了（共 %2 格）").arg(overlayPainted).arg(overlayCells));
        }

        // ---- W5 profile 里那个 model 字段 ----
        check(SkinModel::isSlimVariant(QStringLiteral("slim")) &&
                  SkinModel::isSlimVariant(QStringLiteral("SLIM")),
              "W5 写 slim（不分大小写）就是纤细模型");
        check(!SkinModel::isSlimVariant(QStringLiteral("classic")) &&
                  !SkinModel::isSlimVariant(QString()) &&
                  !SkinModel::isSlimVariant(QStringLiteral("unknown")),
              "W5 classic / 空 / 没见过的值都当经典 —— 宁可画粗一点，也别因为一个怪字符串把手臂画瘦");

        // ---- W6 预览控件：拖动 / 滚轮 / 双击 ----
        SkinViewer viewer;
        viewer.resize(180, 240);
        const auto sendMouse = [&viewer](QEvent::Type type, const QPointF &pos) {
            QMouseEvent e(type, pos, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&viewer, &e);
        };
        const auto sendWheel = [&viewer](int notches) {
            const QPointF pos(90, 120);
            QWheelEvent e(pos, pos, QPoint(), QPoint(0, notches * 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&viewer, &e);
        };
        check(viewer.yaw() == SkinViewer::defaultYaw() &&
                  viewer.pitch() == SkinViewer::defaultPitch() &&
                  viewer.zoom() == SkinViewer::defaultZoom(),
              "W6 开窗就是默认视角（3/4 侧、略微俯视），不是正对着脸");
        check(viewer.skin().isNull(), "W6 没喂贴图之前是空的（兜底那套是界面层喂的）");
        const QImage defSkin = SkinModel::defaultSteveSkin();
        viewer.setSkin(defSkin, QImage(), true);
        check(viewer.skin().size() == QSize(64, 64) && viewer.isSlim() && viewer.cape().isNull(),
              "W6 喂进去的贴图读得回来（验证工程靠这个判\"改到界面上了没有\"）");
        viewer.setSkin(defSkin, QImage(), false);

        sendMouse(QEvent::MouseButtonPress, QPointF(90, 120));
        sendMouse(QEvent::MouseMove, QPointF(130, 130));
        sendMouse(QEvent::MouseButtonRelease, QPointF(130, 130));
        // 横着是**减**：往右拖 = 把正面往右推（yaw 正数反而把正面推向画面左边，见
        // SkinViewer::mouseMoveEvent 里那段 NOTE）。竖着是加：往下拖 = 相机升高。
        check(qAbs(viewer.yaw() - (SkinViewer::defaultYaw() - 20.0)) < 0.001 &&
                  qAbs(viewer.pitch() - (SkinViewer::defaultPitch() + 5.0)) < 0.001,
              "W6 往右下拖 40×10 px → 水平 -20°、俯仰 +5°（0.5°/px，往下拖=相机升高）",
              QString("%1 / %2").arg(viewer.yaw()).arg(viewer.pitch()));

        // 上面那条钉的是**角度**，这条钉的是**手感**：拖动的方向必须和小人转的方向一致。
        // 角度怎么编号是内部约定（yaw、pitch 谁加谁减无所谓），但"往右拖、正面就往右走"
        // 是用户看得见的事实 —— 只钉角度的话，把两边一起改反了断言照样绿。
        // 用正面正中间那一点（y=16、z=2 是身体的正面）当"正面"的代表。
        const QVector3D chestFront(0.0f, 16.0f, 2.0f);
        const auto chestScreenX = [&chestFront](double yaw) {
            const SkinModel::Camera cam =
                SkinModel::makeCamera(QRectF(0, 0, 180, 240), yaw, 0.0, 1.0);
            return SkinModel::project(cam, chestFront).x();
        };
        const double xAtRest = chestScreenX(viewer.yaw());
        sendMouse(QEvent::MouseButtonPress, QPointF(90, 120));
        sendMouse(QEvent::MouseMove, QPointF(50, 120));  // 往**左**拖 40 px
        sendMouse(QEvent::MouseButtonRelease, QPointF(50, 120));
        const double xAfterLeftDrag = chestScreenX(viewer.yaw());
        check(xAfterLeftDrag < xAtRest - 1.0,
              "W6 往左拖 → 小人往左转（正面在画面上真的往左挪了，不是反的）",
              QString("%1 → %2（yaw %3 → %4）").arg(xAtRest).arg(xAfterLeftDrag)
                  .arg(SkinViewer::defaultYaw()).arg(viewer.yaw()));
        sendMouse(QEvent::MouseButtonDblClick, QPointF(90, 120));  // 复位，接着往下测

        const double zoomBefore = viewer.zoom();
        sendWheel(1);
        check(viewer.zoom() > zoomBefore, "W6 往上滚一格放大",
              QString::number(viewer.zoom()));
        sendWheel(-1);
        check(qAbs(viewer.zoom() - zoomBefore) < 1e-9,
              "W6 放大再缩小原路回到原倍数（缩放是乘法不是加法）",
              QString("%1 vs %2").arg(viewer.zoom()).arg(zoomBefore));
        for (int i = 0; i < 40; ++i) {
            sendWheel(1);
        }
        check(viewer.zoom() == SkinModel::maxZoom(), "W6 一直放大到上限就停住",
              QString::number(viewer.zoom()));
        for (int i = 0; i < 80; ++i) {
            sendWheel(-1);
        }
        check(viewer.zoom() == SkinModel::minZoom(), "W6 一直缩小到下限就停住",
              QString::number(viewer.zoom()));

        sendMouse(QEvent::MouseButtonPress, QPointF(90, 120));
        sendMouse(QEvent::MouseMove, QPointF(90, 1200));
        sendMouse(QEvent::MouseButtonRelease, QPointF(90, 1200));
        check(viewer.pitch() == SkinModel::maxPitch() && SkinModel::maxPitch() < 90.0,
              "W6 俯仰角被夹住 —— 正好 90° 会让相机那对基向量退化成零向量、整幅画面空白",
              QString::number(viewer.pitch()));
        sendMouse(QEvent::MouseButtonPress, QPointF(90, 120));
        sendMouse(QEvent::MouseMove, QPointF(90, -1000));
        sendMouse(QEvent::MouseButtonRelease, QPointF(90, -1000));
        check(viewer.pitch() == -SkinModel::maxPitch(), "W6 往另一边也是同一个上限",
              QString::number(viewer.pitch()));

        sendMouse(QEvent::MouseButtonDblClick, QPointF(90, 120));
        check(viewer.yaw() == SkinViewer::defaultYaw() &&
                  viewer.pitch() == SkinViewer::defaultPitch() &&
                  viewer.zoom() == SkinViewer::defaultZoom(),
              "W6 双击复位（这个控件上没有任何按钮，转晕了只能靠它回去）",
              QString("%1 / %2 / %3").arg(viewer.yaw()).arg(viewer.pitch()).arg(viewer.zoom()));

        // ---- W7 光栅化：真的把面画到像素上了 ----
        const auto renderTo = [](const SkinModel::Scene &scene) {
            QImage shot(180, 240, QImage::Format_ARGB32);
            shot.fill(Qt::transparent);  // 背景透明：这样"画上去的像素"数起来干净
            QPainter painter(&shot);
            SkinModel::render(painter, QRectF(shot.rect()), scene);
            painter.end();
            return shot;
        };
        const auto countColor = [](const QImage &img, const QColor &color) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    if (img.pixelColor(x, y) == color) {
                        ++n;
                    }
                }
            }
            return n;
        };

        // 一张**每一面一个颜色**的合成贴图：脸红、后脑蓝。这样"看见的是哪一面"
        // 就直接等于"哪种颜色出现在画面上"，不用去猜投影坐标。
        const QColor frontRed(255, 0, 0);
        const QColor backBlue(0, 0, 255);
        // 披风那两块**涂成两种颜色**（不是只涂一块）：披风图上左边那块 (1,1,10,16) 才是
        // 朝外给人看的，右边那块 (12,1,10,16) 贴着身体。两块分开涂，从背后看见的是**哪种**
        // 颜色就直接说明挂了哪一块 —— 这是"披风放反了"那个 bug 的回归断言。
        const QColor capeOuter(0, 255, 0);
        const QColor capeInner(255, 0, 255);
        QImage marked(64, 64, QImage::Format_ARGB32_Premultiplied);
        marked.fill(Qt::transparent);
        {
            QPainter p(&marked);
            p.fillRect(SkinModel::faceRect(QPoint(0, 0), 8, 8, 8, Face::Front), frontRed);
            p.fillRect(SkinModel::faceRect(QPoint(0, 0), 8, 8, 8, Face::Back), backBlue);
        }
        QImage capeTex(64, 32, QImage::Format_ARGB32_Premultiplied);
        capeTex.fill(Qt::transparent);
        {
            QPainter p(&capeTex);
            p.fillRect(SkinModel::faceRect(QPoint(0, 0), 10, 16, 1, Face::Front), capeOuter);
            p.fillRect(SkinModel::faceRect(QPoint(0, 0), 10, 16, 1, Face::Back), capeInner);
        }

        SkinModel::Scene frontScene;
        frontScene.skin = marked;
        frontScene.cape = capeTex;
        const QImage shotFront = renderTo(frontScene);
        check(countColor(shotFront, frontRed) > 200, "W7 正对着看：脸画出来了（红）",
              QString::number(countColor(shotFront, frontRed)));
        check(countColor(shotFront, backBlue) == 0,
              "W7 正对着看：后脑那一面被剔掉了（一点蓝都没有）",
              QString::number(countColor(shotFront, backBlue)));
        // 正面对着看：披风**朝外**那块 (1,1,10,16) 一个像素都不许有 —— 它长在法线朝 -z
        // 的那一面上，从正面本来就该被背面剔除掉。这一条和下面转到背后那一条是一对，
        // 两个方向一起才把"哪块图长在哪一面"钉死（单看背后那条的话，把两块图对调就红了，
        // 但要是有人反过来把整块披风挪到身体前面，这条正面的检查会先叫出来）。
        //
        // NOTE: 这张合成贴图**只涂了脑袋**，身体四肢全是透明的（那张图是用来验"看见的是
        // 哪一面"的，不是用来验遮挡的），所以从正面能透过身体看见披风的**内侧**
        // 那块 —— 正面的计数因此是"朝外 0、内侧不少"，而不是两个都 0。
        check(countColor(shotFront, capeOuter) == 0 && countColor(shotFront, capeInner) > 200,
              "W7 正对着看：只看得到披风**内侧**那块 (12,1,10,16)，朝外的 (1,1,10,16) 一点都没有",
              QString("朝外 %1 / 内侧 %2")
                  .arg(countColor(shotFront, capeOuter))
                  .arg(countColor(shotFront, capeInner)));

        SkinModel::Scene backScene = frontScene;
        backScene.yaw = 180.0;
        const QImage shotBack = renderTo(backScene);
        check(countColor(shotBack, backBlue) > 200 && countColor(shotBack, frontRed) == 0,
              "W7 转到背后：反过来，后脑看得见、脸被剔掉",
              QString("蓝 %1 / 红 %2")
                  .arg(countColor(shotBack, backBlue))
                  .arg(countColor(shotBack, frontRed)));
        check(countColor(shotBack, capeOuter) > 200,
              "W7 转到背后：看见的是披风图上**朝外**那块 (1,1,10,16)（还说明它真的挂在 -z 那侧）",
              QString::number(countColor(shotBack, capeOuter)));
        check(countColor(shotBack, capeInner) == 0,
              "W7 转到背后：贴着身体那块 (12,1,10,16) 一个像素都不许露 ——"
              "露了就说明披风又反了（那是用户报过的那个 bug）",
              QString::number(countColor(shotBack, capeInner)));

        // 默认外观：画出来的得是个"有颜色的小人"，不是空白、也不是糊成一团。
        SkinModel::Scene defaultScene;
        defaultScene.skin = defSkin;
        defaultScene.yaw = defYaw;
        defaultScene.pitch = defPitch;
        const QImage shotDefault = renderTo(defaultScene);
        int painted = 0;
        QHash<QRgb, int> colorCounts;
        for (int y = 0; y < shotDefault.height(); ++y) {
            for (int x = 0; x < shotDefault.width(); ++x) {
                const QRgb rgb = shotDefault.pixel(x, y);
                if (qAlpha(rgb) == 0) {
                    continue;
                }
                ++painted;
                colorCounts[rgb] = colorCounts.value(rgb) + 1;
            }
        }
        check(painted > 0.05 * 180 * 240 && painted < 0.75 * 180 * 240,
              "W7 默认外观画出来了，占画面 5%~75%（既不是空白，也不是糊满一整块）",
              QString("%1 px").arg(painted));
        // "不是糊成一团"这一条**照着源图算**，不再写死"至少四块"。
        //
        // NOTE: 老的那张兜底皮肤是肤色 / 棕发 / 青上衣 / 蓝裤 / 灰鞋好几块颜色，所以当时写
        // "至少四块各占 2% 的纯色"够用。 用户把兜底图换成了整身 #000000、
        // 脸上一枚 #ffffff 问号（见 W4b），源图**就只有两种颜色** —— 再按"至少四块"
        // 量就是在量一个不存在的东西（那一条当时就是这么红的）。换成拿源图里那几种
        // 不透明色当期望值之后，判据反而更硬，而且以后换配色不用动这儿。
        //
        // ① 画面上不许出现源图里没有的颜色 —— **这一条才是"没糊"的真判据**：插值、抗锯齿、
        // 逐面照明里任意一点半调子混色都会凭空造出第三种颜色，跑不掉。
        QHash<QRgb, int> sourceColors;
        for (int y = 0; y < defSkin.height(); ++y) {
            for (int x = 0; x < defSkin.width(); ++x) {
                const QRgb rgb = defSkin.pixel(x, y);
                if (qAlpha(rgb) != 0) {
                    sourceColors[rgb] = sourceColors.value(rgb) + 1;
                }
            }
        }
        int invented = 0;
        for (auto it = colorCounts.constBegin(); it != colorCounts.constEnd(); ++it) {
            if (!sourceColors.contains(it.key())) {
                ++invented;
            }
        }
        check(invented == 0,
              "W7 默认外观画出来的是**源图里那几种色**，一个没多"
              "（多出来 = 糊了 / 插值 / 抗锯齿 / 逐面照明混色）",
              QString("源图 %1 种 / 画面上 %2 种 / 多出 %3 种")
                  .arg(sourceColors.size())
                  .arg(colorCounts.size())
                  .arg(invented));

        // ② 画面上至少得有**两种**颜色，而且最大那块占一成以上 —— 一种就是"整个小人死色
        // 一块"，也就等于脸上那枚问号**根本没画出来**（源图是黑体 + 白问号两色，所以
        // "看得见第二种颜色"和"问号露脸了"是同一件事，这儿不用去认那个色号）。
        int widest = 0;
        for (auto it = colorCounts.constBegin(); it != colorCounts.constEnd(); ++it) {
            widest = qMax(widest, it.value());
        }
        check(colorCounts.size() >= 2 && widest >= painted / 10,
              "W7 是分块的像素画（不是一整块死色）：至少两种颜色、最大那块占一成以上"
              "（第二种颜色看不见 = 脸上那枚问号没画出来）",
              QString("%1 种 / 最大 %2 px（共 %3）")
                  .arg(colorCounts.size())
                  .arg(widest)
                  .arg(painted));

        // 相邻两个面之间**不许有缝**。
        //
        // NOTE: 这条是踩出来的。原先的写法是"画整张贴图 + 把裁剪框设成那个面的 uv 矩形"，
        // 而裁剪是按每个目标像素的采样点判的：采样点落在矩形外的那个像素整个被丢掉，
        // 于是每一面都比它该占的范围小半圈，模型上到处是一条像素宽的背景色细缝
        // （最明显的是脑袋左边那一条）。改成"先把那一块裁下来再画"才严丝合缝。
        // 拿默认外观肉眼看截图是发现不了的（缝只有一像素，缩略图上像描边）。
        //
        // 判法：把**同一个方块上相邻的两个面**涂成不同颜色，再沿着它们共用的那条棱扫，
        // 两种颜色之间不许夹着背景色。不判"整行/整列连不连"是因为小人本身是凹的
        // （胳肢窝、手底下、两腿之间都是空的），那种判法会把本来就该露背景的地方
        // 也算成缝 —— 实际踩过一次。
        {
            const QColor frontRed(255, 0, 0);
            const QColor leftBlue(0, 0, 255);
            const QColor topGreen(0, 255, 0);
            QImage edges(64, 64, QImage::Format_ARGB32_Premultiplied);
            edges.fill(Qt::transparent);
            {
                QPainter p(&edges);
                // 只画**右腿**的三个面：正面、顶面、左面（+x）。它们两两相邻，共用两条棱
                // —— 一条横的（顶/正，胯前那条）、一条竖的（正/左，腿外侧那条）。
                // 挑腿不挑头是因为默认视角下相机在 y≈29，比头顶（32）**低**，头顶那一面
                // 是背面、被剔掉的（要先发现这点才知道该拿哪个部件验）。腿整条都在相机
                // 下方，三个面都看得见。
                // 其余全透明，所以画面上只剩这三个色块，扫起来没有干扰（覆盖层那几张是
                // 透明的，画了也等于没画）。
                const QPoint leg(0, 16);
                p.fillRect(SkinModel::faceRect(leg, 4, 12, 4, SkinModel::Face::Front), frontRed);
                p.fillRect(SkinModel::faceRect(leg, 4, 12, 4, SkinModel::Face::Left), leftBlue);
                p.fillRect(SkinModel::faceRect(leg, 4, 12, 4, SkinModel::Face::Top), topGreen);
            }
            SkinModel::Scene edgeScene;
            edgeScene.skin = edges;
            edgeScene.yaw = SkinModel::defaultYaw();
            edgeScene.pitch = SkinModel::defaultPitch();
            // 这一条必须在**立正**那一帧上看：腿一摆，胯前那条棱就跟着转，顶面会在
            // 这个视角下被剔掉（它是背对镜头的），下面那两条"扫到了吗"的兜底断言
            // （columnsChecked > 20）会当场红 —— 那不是缝，是这条判法本身就只对立正成立。
            edgeScene.walkPhase = SkinModel::restWalkPhase();
            const QImage shot = renderTo(edgeScene);

            // 横着那条棱：先看到顶面（绿），往下接着正面（红），中间不许有透明的。
            int columnsChecked = 0;
            int horizontalSeams = 0;
            for (int x = 0; x < shot.width(); ++x) {
                int lastGreen = -1;
                int firstRedAfter = -1;
                for (int y = 0; y < shot.height(); ++y) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c == topGreen) {
                        lastGreen = y;
                    } else if (c == frontRed && lastGreen >= 0 && firstRedAfter < 0) {
                        firstRedAfter = y;
                    }
                }
                if (lastGreen < 0 || firstRedAfter < 0) {
                    continue;
                }
                ++columnsChecked;
                for (int y = lastGreen + 1; y < firstRedAfter; ++y) {
                    if (qAlpha(shot.pixel(x, y)) == 0) {
                        ++horizontalSeams;
                    }
                }
            }
            // 竖着那条棱：正面（红）往右接着左面（蓝），中间同样不许有透明的。
            int rowsChecked = 0;
            int verticalSeams = 0;
            for (int y = 0; y < shot.height(); ++y) {
                int lastRed = -1;
                int firstBlueAfter = -1;
                for (int x = 0; x < shot.width(); ++x) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c == frontRed) {
                        lastRed = x;
                    } else if (c == leftBlue && lastRed >= 0 && firstBlueAfter < 0) {
                        firstBlueAfter = x;
                    }
                }
                if (lastRed < 0 || firstBlueAfter < 0) {
                    continue;
                }
                ++rowsChecked;
                for (int x = lastRed + 1; x < firstBlueAfter; ++x) {
                    if (qAlpha(shot.pixel(x, y)) == 0) {
                        ++verticalSeams;
                    }
                }
            }
            // 先确认这条断言真的扫到了东西：两条棱都得有几十行/列在查。
            check(columnsChecked > 20 && rowsChecked > 20,
                  "W7 面上没有缝这条断言本身扫到了东西（胯前那条棱、腿外侧那条棱）",
                  QString("横着查了 %1 列、竖着查了 %2 行").arg(columnsChecked).arg(rowsChecked));
            check(horizontalSeams == 0 && verticalSeams == 0,
                  "W7 相邻两个面严丝合缝：共用那条棱上不许露出背景色"
                  "（面被裁小半圈的话这里会数出一圈缝）",
                  QString("横缝 %1 个像素、竖缝 %2 个像素").arg(horizontalSeams).arg(verticalSeams));

            // 老格式（64×32）补齐了左臂左腿之后，画出来的像素数应该和新格式差不多 ——
            // 缺那两个部件的话会差一大截（右边那块的内侧面还会顶上来，因为挡住它的
            // 左腿不在了）。这条钉的是"老皮肤不会少半拉身子"。
            const auto paintedCount = [&renderTo](const QImage &skin) {
                SkinModel::Scene scene;
                scene.skin = skin;
                scene.yaw = SkinModel::defaultYaw();
                scene.pitch = SkinModel::defaultPitch();
                const QImage s = renderTo(scene);
                int n = 0;
                for (int y = 0; y < s.height(); ++y) {
                    for (int x = 0; x < s.width(); ++x) {
                        if (qAlpha(s.pixel(x, y)) != 0) {
                            ++n;
                        }
                    }
                }
                return n;
            };
            const QColor solid(120, 60, 200);
            QImage legacyUniform(64, 32, QImage::Format_ARGB32_Premultiplied);
            legacyUniform.fill(solid);
            QImage modernUniform(64, 64, QImage::Format_ARGB32_Premultiplied);
            modernUniform.fill(solid);
            const int legacyPainted = paintedCount(legacyUniform);
            const int modernPainted = paintedCount(modernUniform);
            check(legacyPainted > 0.9 * modernPainted && legacyPainted <= modernPainted,
                  "W7 老格式（64×32）的皮肤也画得出整个人：左臂左腿是照右边镜像补的"
                  "（不补的话这两块会去取贴图外面，整块不画）",
                  QString("老格式 %1 px、新格式 %2 px").arg(legacyPainted).arg(modernPainted));
        }

        // ---- W8 老格式（64×32）的左臂左腿：照右边那份**镜像**补出来 ----
        {
            QImage legacy(64, 32, QImage::Format_ARGB32_Premultiplied);
            legacy.fill(Qt::transparent);
            const QRect legFront =
                SkinModel::faceRect(QPoint(0, 16), 4, 12, 4, SkinModel::Face::Front);
            {
                QPainter p(&legacy);
                // 右腿正面涂成"左红右蓝"。镜像要是左右弄反了，补出来的左腿正面
                // 就会是左红右蓝，而不是左蓝右红 —— 这种错在小尺寸预览里根本看不出来。
                p.fillRect(QRect(legFront.x(), legFront.y(), 2, legFront.height()), Qt::red);
                p.fillRect(QRect(legFront.x() + 2, legFront.y(), 2, legFront.height()), Qt::blue);
                p.fillRect(QRect(8, 8, 1, 1), Qt::green);  // 头正面的一角：补齐时不许动它
            }
            const QImage fixed = SkinModel::completeLegacySkin(legacy);
            check(fixed.size() == QSize(64, 64), "W8 老格式补齐之后是 64×64（新格式本来就够大）",
                  QString("%1×%2").arg(fixed.width()).arg(fixed.height()));

            const QRect leftLegFront =
                SkinModel::faceRect(QPoint(16, 48), 4, 12, 4, SkinModel::Face::Front);
            check(fixed.pixelColor(leftLegFront.x(), leftLegFront.y()) == QColor(Qt::blue) &&
                      fixed.pixelColor(leftLegFront.right(), leftLegFront.y()) == QColor(Qt::red),
                  "W8 左腿正面是右腿正面**左右翻过来**的（镜像反了的话这两块颜色会对调）",
                  QString("%1 / %2")
                      .arg(fixed.pixelColor(leftLegFront.x(), leftLegFront.y()).name())
                      .arg(fixed.pixelColor(leftLegFront.right(), leftLegFront.y()).name()));

            // 左臂也要补上（配对和镜像跟腿是同一套，但**基准点不同**，所以单独看一眼）。
            const QPoint armBase(40, 16);
            const QRect armFront = SkinModel::faceRect(armBase, 4, 12, 4, SkinModel::Face::Front);
            {
                QPainter p(&legacy);
                p.fillRect(QRect(armFront.x(), armFront.y(), 2, armFront.height()), Qt::yellow);
            }
            const QImage fixed2 = SkinModel::completeLegacySkin(legacy);
            const QRect leftArmFront =
                SkinModel::faceRect(QPoint(32, 48), 4, 12, 4, SkinModel::Face::Front);
            check(fixed2.pixelColor(leftArmFront.right(), leftArmFront.y()) == QColor(Qt::yellow),
                  "W8 左臂正面也补上了（黄色那块翻到了另一边）",
                  fixed2.pixelColor(leftArmFront.right(), leftArmFront.y()).name());

            check(fixed.pixelColor(8, 8) == QColor(Qt::green),
                  "W8 补齐只动左臂左腿那两块，头身体那半边一个字不改",
                  fixed.pixelColor(8, 8).name());
            // 第二层那几块（老格式根本没有）补齐之后仍然是透明的：不透明的话会多画一层壳。
            check(qAlpha(fixed.pixel(48, 48)) == 0 && qAlpha(fixed.pixel(0, 48)) == 0,
                  "W8 补齐出来的左臂左腿**没有**第二层（老格式本来就没有，补上就成了多一层壳）");

            QImage modern(64, 64, QImage::Format_ARGB32_Premultiplied);
            modern.fill(Qt::transparent);
            modern.setPixelColor(30, 30, QColor(12, 34, 56));
            check(SkinModel::completeLegacySkin(modern) == modern,
                  "W8 新格式（64×64）进来的原样出去，一个像素都不碰");
        }

        // 控件被压到 1×1（窗口拖到极窄、布局把这一栏挤没）时不许崩：那种尺寸下投影出来
        // 的四边形是退化的，quadToQuad 会返回 false —— 这条就是走那条路。
        {
            SkinViewer tiny;
            tiny.resize(1, 1);
            QPixmap shot(1, 1);
            tiny.render(&shot);
            check(true, "W7 1×1 的控件渲染不崩（退化四边形那条路走通了）");
        }

        // ---- W9 走路姿势：摆多少度、左右反相、披风翘起来 ----
        //
        // 这些数（32° / 23° / 6° / 5°）是**产品观感**的参数，不是随手挑的：照游戏里
        // ModelBiped 的系数折算（见 SkinModel.cpp 里那一段）。所以这里钉的是具体数字，
        // 谁改了都会当场红 —— 想改观感就一起改这里的数，别让它们悄悄漂。
        {
            const double rest = SkinModel::restWalkPhase();
            const QVector<SkinModel::Cuboid> stride = SkinModel::walkingParts(false, 0.0);
            const QVector<SkinModel::Cuboid> upright = SkinModel::walkingParts(false, rest);
            const auto partOf = [&boxNamed](const QVector<SkinModel::Cuboid> &boxes,
                                            const QString &name) { return boxNamed(boxes, name); };
            const auto deg = [](const SkinModel::Cuboid &box) {
                return static_cast<double>(box.rotation.x());
            };

            // 相位 0 = 迈得最大：右腿往后 32°、左腿往前 32°（一正一负 = 反相）。
            check(qAbs(deg(partOf(stride, QStringLiteral("右腿"))) - 32.0) < 0.001 &&
                      qAbs(deg(partOf(stride, QStringLiteral("左腿"))) + 32.0) < 0.001,
                  "W9 迈到最大的那一帧：右腿往后 32°、左腿往前 32°（游戏里 1.4×0.4 折出来的）",
                  QString("右 %1 / 左 %2")
                      .arg(deg(partOf(stride, QStringLiteral("右腿"))))
                      .arg(deg(partOf(stride, QStringLiteral("左腿")))));
            // 手臂跟**同侧的腿**反相、幅度小一半（游戏里就是这样，1:1 摆像在走正步）。
            check(qAbs(deg(partOf(stride, QStringLiteral("右臂"))) + 23.0) < 0.001 &&
                      qAbs(deg(partOf(stride, QStringLiteral("左臂"))) - 23.0) < 0.001,
                  "W9 同一帧：右臂往前 23°、左臂往后 23°（跟同侧的腿反相、幅度比腿小）",
                  QString("右 %1 / 左 %2")
                      .arg(deg(partOf(stride, QStringLiteral("右臂"))))
                      .arg(deg(partOf(stride, QStringLiteral("左臂")))));

            // 半个来回之后**左右互换**（是"走过去又走回来"，不是四条肢体一起荡）。
            const QVector<SkinModel::Cuboid> swapped = SkinModel::walkingParts(false, 0.5);
            check(qAbs(deg(partOf(swapped, QStringLiteral("右腿"))) + 32.0) < 0.001 &&
                      qAbs(deg(partOf(swapped, QStringLiteral("左腿"))) - 32.0) < 0.001,
                  "W9 相位过半之后两条腿对调 —— 一前一后是交替的，不是四条一起荡",
                  QString("右 %1 / 左 %2")
                      .arg(deg(partOf(swapped, QStringLiteral("右腿"))))
                      .arg(deg(partOf(swapped, QStringLiteral("左腿")))));

            // 立正那一帧：四条肢体的角度都回到 0（这一帧上的几何和 playerCuboids 一样，
            // W7 那条「面与面之间没有缝」的判法就是拿它当基准的）。**关节位置不受影响** ——
            // 姿势只填角度，不挪关节。
            double biggest = 0.0;
            QString worst;
            bool jointMoved = false;
            for (const QString &name : {QStringLiteral("右臂"), QStringLiteral("左臂"),
                                        QStringLiteral("右腿"), QStringLiteral("左腿")}) {
                const SkinModel::Cuboid staged = partOf(upright, name);
                const SkinModel::Cuboid still = partOf(parts, name);
                if (qAbs(deg(staged)) > biggest) {
                    biggest = qAbs(deg(staged));
                    worst = name;
                }
                if (staged.pivot != still.pivot) {
                    jointMoved = true;
                }
            }
            check(biggest < 1e-9 && !jointMoved,
                  "W9 立正那一帧四肢角度正好是 0（关节位置是几何事实，姿势不许动它）",
                  QString("%1 %2°、关节被挪过 %3")
                      .arg(worst)
                      .arg(biggest)
                      .arg(jointMoved ? QStringLiteral("是") : QStringLiteral("否")));

            // 头和身体不摆姿势：走路时它们整块跟着走，没有单独的关节。
            check(deg(partOf(stride, QStringLiteral("头"))) == 0.0 &&
                      deg(partOf(stride, QStringLiteral("身体"))) == 0.0,
                  "W9 头和身体不吃走路的角度（它们没有单独转的道理）");

            // 披风：仰角是**常量** 24°（用户要的 20~30° 那一档），不跟着步频晃 ——
            // 它翘起来是为了别挡住身体，跟着一翘一落反而把眼睛全引到披风上去。
            // walkingCape() 连相位都不收（签名里留着相位，读的人就会以为它跟步频有关）。
            const SkinModel::Cuboid liftedCape = SkinModel::walkingCape();
            check(qAbs(liftedCape.rotation.x() - 24.0) < 0.001,
                  "W9 披风固定往后翘 24°（用户要的 20~30° 那一档），跟步频无关",
                  QString("%1°").arg(liftedCape.rotation.x()));
            check(qAbs(liftedCape.rotation.y() - 180.0) < 0.001,
                  "W9 摆姿势不许碰那 180° —— 它是「朝外取哪块图」的出处（见 W2）",
                  QString::number(liftedCape.rotation.y()));

            // 光看"填了多少度"不算数，看**真的离身体更远了**：披风朝外那面四个角的
            // 平均 z 跟垂着的那块比。**方向**也在这里钉着：正负号写反的话下摆会扎进
            // 身体里，那个差值就变成正的，这条会当场红。
            const auto faceMeanZ = [](const SkinModel::Cuboid &box) {
                const SkinModel::FaceGeometry g = SkinModel::faceGeometry(box, Face::Front);
                double sum = 0.0;
                for (const QVector3D &corner : g.corners) {
                    sum += corner.z();
                }
                return sum / g.corners.size();
            };
            check(faceMeanZ(liftedCape) < faceMeanZ(SkinModel::capeCuboid()) - 3.0,
                  "W9 披风的下摆**真的**甩到身后去了（那一面的平均 z 更负），"
                  "不是只把数字填进字段里",
                  QString("垂着 z %1 / 翘起 z %2")
                      .arg(faceMeanZ(SkinModel::capeCuboid()), 0, 'f', 2)
                      .arg(faceMeanZ(liftedCape), 0, 'f', 2));
            // 翘的是**下摆**：上沿还得挂在肩膀那条线（y=24）上，不然整件披风会从肩上飘走
            // （关节定错了就是这个下场）。容差 0.5 是那块板的厚度转出来的那一点。
            double capeTopY = -1e9;
            for (const QVector3D &corner :
                 SkinModel::faceGeometry(liftedCape, Face::Front).corners) {
                capeTopY = qMax(capeTopY, corner.y());
            }
            check(qAbs(capeTopY - 24.0) < 0.5,
                  "W9 翘的只是下摆：披风上沿还挂在肩膀上（y≈24）",
                  QString("上沿 y %1").arg(capeTopY, 0, 'f', 2));
        }

        // ---- W9b 控件里那点动画：相位怎么走、什么时候停 ----
        {
            SkinViewer walker;
            walker.resize(180, 240);
            walker.setSkin(defSkin, QImage(), false);

            walker.setAnimated(false);
            walker.setWalkPhase(0.0);
            check(qAbs(walker.walkPhase()) < 1e-9, "W9 关掉动画 + 钉住相位 0 → 就停在 0",
                  QString::number(walker.walkPhase()));
            // 相位是个环：传 1.25 和传 0.25 是同一帧（调用方不用自己折）。
            walker.setWalkPhase(1.25);
            check(qAbs(walker.walkPhase() - 0.25) < 1e-9,
                  "W9 相位自动折到 0..1（传 1.25 读回来是 0.25）",
                  QString::number(walker.walkPhase()));
            const auto spin = [](int ms) {
                QElapsedTimer span;
                span.start();
                while (span.elapsed() < ms) {
                    QApplication::processEvents();
                }
            };
            spin(60);
            check(qAbs(walker.walkPhase() - 0.25) < 1e-9,
                  "W9 动画关着的时候，时间过去相位也不动 —— 截图才可复现",
                  QString::number(walker.walkPhase()));

            // 显示出来 + 动画开着 → 相位自己往前走（定时器真的接上了）。
            walker.show();
            walker.setAnimated(true);
            const double before = walker.walkPhase();
            QElapsedTimer waited;
            waited.start();
            bool moved = false;
            while (waited.elapsed() < 400 && !moved) {
                QApplication::processEvents();
                moved = qAbs(walker.walkPhase() - before) > 1e-6;
            }
            check(moved, "W9 显示出来 + 动画开着 → 相位自己往前走（就是这一条让它看起来在走）",
                  QString("%1 → %2").arg(before).arg(walker.walkPhase()));

            // 藏起来就停（切到别的页、最小化）：停下之后相位定住不动。
            walker.hide();
            QApplication::processEvents();
            const double paused = walker.walkPhase();
            spin(60);
            check(qAbs(walker.walkPhase() - paused) < 1e-9,
                  "W9 藏起来就停：不该在看不见的时候还在后台每秒重画 60 次",
                  QString("%1 → %2").arg(paused).arg(walker.walkPhase()));
            // 再显示出来接着走（从停下那一帧起，而不是跳回 0）。
            walker.show();
            spin(60);
            check(qAbs(walker.walkPhase() - paused) > 1e-6,
                  "W9 再显示出来接着走，而且是从停下那一帧接着（不是跳回 0）",
                  QString("%1 → %2").arg(paused).arg(walker.walkPhase()));
            walker.hide();
            // 藏着的控件上把动画打开，也不许开始转 —— "只在看得见的时候走"这条不能留后门
            // （另一个入口是 showEvent，显示出来的时候它自己会开）。
            walker.setAnimated(false);
            walker.setAnimated(true);
            const double stillHidden = walker.walkPhase();
            spin(60);
            check(qAbs(walker.walkPhase() - stillHidden) < 1e-9,
                  "W9 藏着的控件上开动画也先不转（要等显示出来那条路去开定时器）",
                  QString("%1 → %2").arg(stillHidden).arg(walker.walkPhase()));
        }

        // 姿势真的落到了像素上：同一个视角，迈步那一帧和立正那一帧画出来不一样
        // （**侧着看**差别最大 —— 腿前后摆，正面看几乎看不出区别）。
        {
            const auto sideShot = [&renderTo](double phase) {
                SkinModel::Scene scene;
                scene.skin = SkinModel::defaultSteveSkin();
                scene.yaw = 90.0;
                scene.pitch = 0.0;
                scene.walkPhase = phase;
                return renderTo(scene);
            };
            const QImage upright = sideShot(SkinModel::restWalkPhase());
            const QImage stride = sideShot(0.0);
            int diff = 0;
            for (int y = 0; y < upright.height(); ++y) {
                for (int x = 0; x < upright.width(); ++x) {
                    if (upright.pixel(x, y) != stride.pixel(x, y)) {
                        ++diff;
                    }
                }
            }
            check(diff > 200, "W9 迈步和立正画出来确实不一样 —— 姿势走到了光栅化那一步",
                  QString("%1 px 不同").arg(diff));
        }
    }
    return 0;
}


