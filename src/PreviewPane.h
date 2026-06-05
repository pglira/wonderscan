#pragma once

#include <QWidget>
#include <QImage>
#include <QVector>

// Shows the live dewarped result: one rectified page, or two side by side.
class PreviewPane : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPane(QWidget *parent = nullptr);

    void setPages(const QVector<QImage> &pages);
    void clear();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QVector<QImage> m_pages;
};
