#include "tools/cabana/videowidget.h"

#include <algorithm>
#include <utility>

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedLayout>
#include <QStyleOptionSlider>
#include <QVBoxLayout>
#include <QtConcurrent>

const int MIN_VIDEO_HEIGHT = 100;
const int THUMBNAIL_MARGIN = 3;

static const QColor timeline_colors[] = {
  [(int)TimelineType::None] = QColor(111, 143, 175),
  [(int)TimelineType::Engaged] = QColor(0, 163, 108),
  [(int)TimelineType::UserFlag] = Qt::magenta,
  [(int)TimelineType::AlertInfo] = Qt::green,
  [(int)TimelineType::AlertWarning] = QColor(255, 195, 0),
  [(int)TimelineType::AlertCritical] = QColor(199, 0, 57),
};

VideoWidget::VideoWidget(QWidget *parent) : QFrame(parent) {
  setFrameStyle(QFrame::StyledPanel | QFrame::Plain);
  auto main_layout = new QVBoxLayout(this);
  if (!can->liveStreaming()) {
    main_layout->addWidget(createCameraWidget());
  }

  // btn controls
  QButtonGroup *group = new QButtonGroup(this);
  group->setExclusive(true);

  QHBoxLayout *control_layout = new QHBoxLayout();
  play_btn = new QToolButton();
  play_btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  control_layout->addWidget(play_btn);
  if (can->liveStreaming()) {
    control_layout->addWidget(skip_to_end_btn = new QToolButton(this));
    skip_to_end_btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    skip_to_end_btn->setIcon(utils::icon("skip-end-fill"));
    skip_to_end_btn->setToolTip(tr("Skip to the end"));
    QObject::connect(skip_to_end_btn, &QToolButton::clicked, [group]() {
      // set speed to 1.0
      group->buttons()[2]->click();
      can->pause(false);
      can->seekTo(can->totalSeconds() + 1);
    });
  }

  for (float speed : {0.1, 0.5, 1., 2.}) {
    QToolButton *btn = new QToolButton(this);
    btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    btn->setText(QString("%1x").arg(speed));
    btn->setCheckable(true);
    QObject::connect(btn, &QToolButton::clicked, [speed]() { can->setSpeed(speed); });
    control_layout->addWidget(btn);
    group->addButton(btn);
    if (speed == 1.0) btn->setChecked(true);
  }
  main_layout->addLayout(control_layout);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  QObject::connect(play_btn, &QToolButton::clicked, []() { can->pause(!can->isPaused()); });
  QObject::connect(can, &AbstractStream::paused, this, &VideoWidget::updatePlayBtnState);
  QObject::connect(can, &AbstractStream::resume, this, &VideoWidget::updatePlayBtnState);
  QObject::connect(&settings, &Settings::changed, this, &VideoWidget::updatePlayBtnState);
  updatePlayBtnState();

  setWhatsThis(tr(R"(
    <b>Video</b><br />
    <!-- TODO: add descprition here -->
    <span style="color:gray">Timeline color</span>
    <table>
    <tr><td><span style="color:%1;">■ </span>Disengaged </td>
        <td><span style="color:%2;">■ </span>Engaged</td></tr>
    <tr><td><span style="color:%3;">■ </span>User Flag </td>
        <td><span style="color:%4;">■ </span>Info</td></tr>
    <tr><td><span style="color:%5;">■ </span>Warning </td>
        <td><span style="color:%6;">■ </span>Critical</td></tr>
    </table>
    <span style="color:gray">Shortcuts</span><br/>
    Pause/Resume: <span style="background-color:lightGray;color:gray">&nbsp;space&nbsp;</span>
  )").arg(timeline_colors[(int)TimelineType::None].name(),
          timeline_colors[(int)TimelineType::Engaged].name(),
          timeline_colors[(int)TimelineType::UserFlag].name(),
          timeline_colors[(int)TimelineType::AlertInfo].name(),
          timeline_colors[(int)TimelineType::AlertWarning].name(),
          timeline_colors[(int)TimelineType::AlertCritical].name()));
}

QWidget *VideoWidget::createCameraWidget() {
  QWidget *w = new QWidget(this);
  QVBoxLayout *l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);

  QStackedLayout *stacked = new QStackedLayout();
  stacked->setStackingMode(QStackedLayout::StackAll);
  stacked->addWidget(cam_widget = new CameraWidget("camerad", can->visionStreamType(), false));
  cam_widget->setMinimumHeight(MIN_VIDEO_HEIGHT);
  cam_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
  stacked->addWidget(alert_label = new InfoLabel(this));
  l->addLayout(stacked);

  // slider controls
  auto slider_layout = new QHBoxLayout();
  slider_layout->addWidget(time_label = new QLabel("00:00"));

  slider = new Slider(this);
  slider->setSingleStep(0);
  slider_layout->addWidget(slider);

  slider_layout->addWidget(end_time_label = new QLabel(this));
  l->addLayout(slider_layout);

  setMaximumTime(can->totalSeconds());
  QObject::connect(slider, &QSlider::sliderReleased, [this]() { can->seekTo(slider->currentSecond()); });
  QObject::connect(slider, &QSlider::valueChanged, [=](int value) { time_label->setText(utils::formatSeconds(slider->currentSecond())); });
  QObject::connect(slider, &Slider::updateMaximumTime, this, &VideoWidget::setMaximumTime, Qt::QueuedConnection);
  QObject::connect(cam_widget, &CameraWidget::clicked, []() { can->pause(!can->isPaused()); });
  QObject::connect(static_cast<ReplayStream*>(can), &ReplayStream::qLogLoaded, slider, &Slider::parseQLog);
  QObject::connect(can, &AbstractStream::updated, this, &VideoWidget::updateState);
  return w;
}

void VideoWidget::setMaximumTime(double sec) {
  maximum_time = sec;
  end_time_label->setText(utils::formatSeconds(sec));
  slider->setTimeRange(0, sec);
}

void VideoWidget::updateTimeRange(double min, double max, bool is_zoomed) {
  if (can->liveStreaming()) {
    skip_to_end_btn->setEnabled(!is_zoomed);
    return;
  }

  if (!is_zoomed) {
    min = 0;
    max = maximum_time;
  }
  end_time_label->setText(utils::formatSeconds(max));
  slider->setTimeRange(min, max);
}

void VideoWidget::updateState() {
  if (!slider->isSliderDown()) {
    slider->setCurrentSecond(can->currentSec());
  }
  alert_label->showAlert(slider->alertInfo(can->currentSec()));
}

void VideoWidget::updatePlayBtnState() {
  play_btn->setIcon(utils::icon(can->isPaused() ? "play" : "pause"));
  play_btn->setToolTip(can->isPaused() ? tr("Play") : tr("Pause"));
}

// Slider

Slider::Slider(QWidget *parent) : thumbnail_label(parent), QSlider(Qt::Horizontal, parent) {
  setMouseTracking(true);
}

AlertInfo Slider::alertInfo(double seconds) {
  uint64_t mono_time = (seconds + can->routeStartTime()) * 1e9;
  auto alert_it = alerts.lower_bound(mono_time);
  bool has_alert = (alert_it != alerts.end()) && ((alert_it->first - mono_time) <= 1e8);
  return has_alert ? alert_it->second : AlertInfo{};
}

QPixmap Slider::thumbnail(double seconds)  {
  uint64_t mono_time = (seconds + can->routeStartTime()) * 1e9;
  auto it = thumbnails.lowerBound(mono_time);
  return it != thumbnails.end() ? it.value() : QPixmap();
}

void Slider::setTimeRange(double min, double max) {
  assert(min < max);
  setRange(min * factor, max * factor);
}

void Slider::parseQLog(int segnum, std::shared_ptr<LogReader> qlog) {
 const auto &segments = qobject_cast<ReplayStream *>(can)->route()->segments();
  if (segments.size() > 0 && segnum == segments.rbegin()->first && !qlog->events.empty()) {
    emit updateMaximumTime(qlog->events.back()->mono_time / 1e9 - can->routeStartTime());
  }

  std::mutex mutex;
  QtConcurrent::blockingMap(qlog->events.cbegin(), qlog->events.cend(), [&mutex, this](const Event *e) {
    if (e->which == cereal::Event::Which::THUMBNAIL) {
      auto thumb = e->event.getThumbnail();
      auto data = thumb.getThumbnail();
      if (QPixmap pm; pm.loadFromData(data.begin(), data.size(), "jpeg")) {
        QPixmap scaled = pm.scaledToHeight(MIN_VIDEO_HEIGHT - THUMBNAIL_MARGIN * 2, Qt::SmoothTransformation);
        std::lock_guard lk(mutex);
        thumbnails[thumb.getTimestampEof()] = scaled;
      }
    } else if (e->which == cereal::Event::Which::CONTROLS_STATE) {
      auto cs = e->event.getControlsState();
      if (cs.getAlertType().size() > 0 && cs.getAlertText1().size() > 0 &&
          cs.getAlertSize() != cereal::ControlsState::AlertSize::NONE) {
        std::lock_guard lk(mutex);
        alerts.emplace(e->mono_time, AlertInfo{cs.getAlertStatus(), cs.getAlertText1().cStr(), cs.getAlertText2().cStr()});
      }
    }
  });
  update();
}

void Slider::paintEvent(QPaintEvent *ev) {
  QPainter p(this);
  QRect r = rect().adjusted(0, 4, 0, -4);
  p.fillRect(r, timeline_colors[(int)TimelineType::None]);
  double min = minimum() / factor;
  double max = maximum() / factor;

  for (auto [begin, end, type] : qobject_cast<ReplayStream *>(can)->getTimeline()) {
    if (begin > max || end < min)
      continue;
    r.setLeft(((std::max(min, begin) - min) / (max - min)) * width());
    r.setRight(((std::min(max, end) - min) / (max - min)) * width());
    p.fillRect(r, timeline_colors[(int)type]);
  }

  QStyleOptionSlider opt;
  opt.initFrom(this);
  opt.minimum = minimum();
  opt.maximum = maximum();
  opt.subControls = QStyle::SC_SliderHandle;
  opt.sliderPosition = value();
  style()->drawComplexControl(QStyle::CC_Slider, &opt, &p);
}

void Slider::mousePressEvent(QMouseEvent *e) {
  QSlider::mousePressEvent(e);
  if (e->button() == Qt::LeftButton && !isSliderDown()) {
    int value = minimum() + ((maximum() - minimum()) * e->x()) / width();
    setValue(value);
    emit sliderReleased();
  }
}

void Slider::mouseMoveEvent(QMouseEvent *e) {
  int pos = std::clamp(e->pos().x(), 0, width());
  double seconds = (minimum() + pos * ((maximum() - minimum()) / (double)width())) / factor;
  QPixmap thumb = thumbnail(seconds);
  if (!thumb.isNull()) {
    int x = std::clamp(pos - thumb.width() / 2, THUMBNAIL_MARGIN, rect().right() - thumb.width() - THUMBNAIL_MARGIN);
    int y = -thumb.height();
    thumbnail_label.showPixmap(mapToParent({x, y}), utils::formatSeconds(seconds), thumb, alertInfo(seconds));
  } else {
    thumbnail_label.hide();
  }
  QSlider::mouseMoveEvent(e);
}

bool Slider::event(QEvent *event) {
  switch (event->type()) {
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::Leave:
      thumbnail_label.hide();
      break;
    default:
      break;
  }
  return QSlider::event(event);
}

// InfoLabel

InfoLabel::InfoLabel(QWidget *parent) : QWidget(parent, Qt::WindowStaysOnTopHint) {
  setAttribute(Qt::WA_ShowWithoutActivating);
  setVisible(false);
}

void InfoLabel::showPixmap(const QPoint &pt, const QString &sec, const QPixmap &pm, const AlertInfo &alert) {
  second = sec;
  pixmap = pm;
  alert_info = alert;
  resize(pm.size());
  move(pt);
  setVisible(true);
  update();
}

void InfoLabel::showAlert(const AlertInfo &alert) {
  alert_info = alert;
  pixmap = {};
  setVisible(!alert_info.text1.isEmpty());
  update();
}

void InfoLabel::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setPen(QPen(palette().color(QPalette::BrightText), 2));
  if (!pixmap.isNull()) {
    p.drawPixmap(0, 0, pixmap);
    p.drawRect(rect());
    p.drawText(rect().adjusted(0, 0, 0, -THUMBNAIL_MARGIN), second, Qt::AlignHCenter | Qt::AlignBottom);
  }
  if (alert_info.text1.size() > 0) {
    QColor color = timeline_colors[(int)TimelineType::AlertInfo];
    if (alert_info.status == cereal::ControlsState::AlertStatus::USER_PROMPT) {
      color = timeline_colors[(int)TimelineType::AlertWarning];
    } else if (alert_info.status == cereal::ControlsState::AlertStatus::CRITICAL) {
      color = timeline_colors[(int)TimelineType::AlertCritical];
    }
    color.setAlphaF(0.5);
    QString text = alert_info.text1;
    if (!alert_info.text2.isEmpty()) {
      text += "\n" + alert_info.text2;
    }

    if (!pixmap.isNull()) {
      QFont font;
      font.setPixelSize(11);
      p.setFont(font);
    }
    QRect text_rect = rect().adjusted(2, 2, -2, -2);
    QRect r = p.fontMetrics().boundingRect(text_rect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, text);
    p.fillRect(text_rect.left(), r.top(), text_rect.width(), r.height(), color);
    p.drawText(text_rect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, text);
  }
}
