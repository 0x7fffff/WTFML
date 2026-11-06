/**
 * @file SchematicInfoDialog.hpp
 * @brief 双击 / 关联打开 `.nbt` / `.litematic` / `.schematic` 时显示一份只读摘要。
 */
#pragma once

#include <QDialog>
#include <QString>

class SchematicInfoDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SchematicInfoDialog(const QString &path, QWidget *parent = nullptr);
};
