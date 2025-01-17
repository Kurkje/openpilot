#pragma once

#include <map>
#include <memory>

#include <QLabel>
#include <QSlider>
#include <QToolButton>

#include "selfdrive/ui/qt/widgets/cameraview.h"
#include "tools/cabana/streams/replaystream.h"

struct AlertInfo {
  cereal::ControlsState::AlertStatus status;
  QString text1;
  QString text2;
};

class InfoLabel : public QWidget {
public:
  InfoLabel(QWidget *parent);
  void showPixmap(const QPoint &pt, const QString &sec, const QPixmap &pm, const AlertInfo &alert);
  void showAlert(const AlertInfo &alert);
  void paintEvent(QPaintEvent *event) override;
  QPixmap pixmap;
  QString second;
  AlertInfo alert_info;
};

class Slider : public QSlider {
  Q_OBJECT

public:
  Slider(QWidget *parent);
  double currentSecond() const { return value() / factor; }
  void setCurrentSecond(double sec) { setValue(sec * factor); }
  void setTimeRange(double min, double max);
  AlertInfo alertInfo(double sec);
  QPixmap thumbnail(double sec);
  void parseQLog(int segnum, std::shared_ptr<LogReader> qlog);

signals:
  void updateMaximumTime(double);

private:
  void mousePressEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  bool event(QEvent *event) override;
  void paintEvent(QPaintEvent *ev) override;

  const double factor = 1000.0;
  QMap<uint64_t, QPixmap> thumbnails;
  std::map<uint64_t, AlertInfo> alerts;
  InfoLabel thumbnail_label;
};

class VideoWidget : public QFrame {
  Q_OBJECT

public:
  VideoWidget(QWidget *parnet = nullptr);
  void updateTimeRange(double min, double max, bool is_zommed);
  void setMaximumTime(double sec);

protected:
  void updateState();
  void updatePlayBtnState();
  QWidget *createCameraWidget();

  CameraWidget *cam_widget;
  double maximum_time = 0;
  QLabel *end_time_label;
  QLabel *time_label;
  QToolButton *play_btn;
  QToolButton *skip_to_end_btn = nullptr;
  InfoLabel *alert_label;
  Slider *slider;
};
