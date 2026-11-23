#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "main_window.h"

#include "lnplayer_windows.h"
#include "player_icons.h"

#include <algorithm>
#include <d3d11.h>
#include <cmath>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QByteArray>
#include <QFont>
#include <QFontMetrics>
#include <QFontComboBox>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDir>
#include <QDesktopServices>
#include <QDirIterator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QCheckBox>
#include <QColor>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QComboBox>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QScreen>
#include <QDropEvent>
#include <QEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QListView>
#include <QListWidgetItem>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyleOptionSlider>
#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QShortcut>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QPainter>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QStringDecoder>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStandardPaths>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{

class SeekSlider final : public QSlider
{
public:
    explicit SeekSlider(QWidget* parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && !sliderHandleRect().contains(event->position().toPoint()))
        {
            groove_dragging_ = true;
            setSliderDown(true);
            setValueFromPosition(event->position().toPoint().x());
            event->accept();
            return;
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (groove_dragging_ && (event->buttons() & Qt::LeftButton))
        {
            setValueFromPosition(event->position().toPoint().x());
            event->accept();
            return;
        }
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (groove_dragging_ && event->button() == Qt::LeftButton)
        {
            setValueFromPosition(event->position().toPoint().x());
            groove_dragging_ = false;
            setSliderDown(false);
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

private:
    QRect sliderHandleRect() const
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        return style()->subControlRect(
            QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
    }

    void setValueFromPosition(int x)
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect handle = style()->subControlRect(
            QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const int span = std::max(1, width() - handle.width());
        const int position = std::clamp(x - handle.width() / 2, 0, span);
        setValue(QStyle::sliderValueFromPosition(
            minimum(), maximum(), position, span, option.upsideDown));
    }

    bool groove_dragging_ = false;
};

QString application_data_directory()
{
    static const QString directory = []
    {
        const QString application_directory = QCoreApplication::applicationDirPath();
        const QString portable_directory = QDir(application_directory).filePath(
            QStringLiteral("Data"));
        if (QDir().mkpath(portable_directory))
        {
            QFile write_probe(QDir(portable_directory).filePath(
                QStringLiteral(".write_test")));
            if (write_probe.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                write_probe.close();
                QFile::remove(write_probe.fileName());
                return portable_directory;
            }
        }

        const QString local_directory = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        const QString fallback_directory = QDir(local_directory).filePath(
            QStringLiteral("Data"));
        QDir().mkpath(fallback_directory);
        return fallback_directory;
    }();
    return directory;
}

QString settings_file_path()
{
    return QDir(application_data_directory()).filePath(
        QStringLiteral("settings.ini"));
}

void initialize_settings_file()
{
    const QString target_path = settings_file_path();
    const QString previous_ini = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("settings.ini"));
    if (!QFile::exists(target_path) && QFile::exists(previous_ini))
        QFile::copy(previous_ini, target_path);

    QSettings settings(target_path, QSettings::IniFormat);
    const QString migration_key = QStringLiteral("Config/LegacySettingsMigrationCompleted");
    if (settings.value(migration_key, false).toBool())
        return;

#ifdef Q_OS_WIN
    QSettings legacy(
        QSettings::NativeFormat,
        QSettings::UserScope,
        QStringLiteral("LNPlayer"),
        QStringLiteral("LNPlayer"));
    for (const QString& key : legacy.allKeys())
    {
        if (!settings.contains(key))
            settings.setValue(key, legacy.value(key));
    }
#endif
    settings.setValue(migration_key, true);
    settings.sync();
}

QString format_position(int64_t position_ms)
{
    const int64_t total_seconds = position_ms < 0 ? 0 : position_ms / 1000;
    const int64_t hours = total_seconds / 3600;
    const int64_t minutes = total_seconds / 60 % 60;
    const int64_t seconds = total_seconds % 60;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}


void draw_subtitle_text(
    QImage& image,
    const QString& subtitle,
    const QString& font_family,
    const QString& color,
    int font_size,
    int vertical_position)
{
    if (image.isNull() || subtitle.isEmpty())
        return;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font(font_family);
    font.setPixelSize(font_size > 0 ? font_size : std::max(20, image.width() / 40));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);

    const QRect text_rect(
        image.width() / 20,
        image.height() * vertical_position / 100,
        image.width() * 9 / 10,
        image.height() / 6);
    const int offsets[][2] = {
        {-2, -2}, {0, -2}, {2, -2}, {-2, 0},
        {2, 0}, {-2, 2}, {0, 2}, {2, 2}
    };
    painter.setPen(Qt::black);
    for (const auto& offset : offsets)
        painter.drawText(
            text_rect.translated(offset[0], offset[1]),
            Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
            subtitle);
    painter.setPen(QColor(color));
    painter.drawText(
        text_rect,
        Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
        subtitle);
}

void draw_subtitle_bitmap(QImage& image, const QImage& bitmap, bool fit_aspect, float aspect)
{
    if (image.isNull() || bitmap.isNull())
        return;

    QPainter painter(&image);
    if (!fit_aspect)
    {
        painter.drawImage(image.rect(), bitmap);
        return;
    }

    if (aspect <= 0.0f)
        aspect = static_cast<float>(bitmap.width()) / bitmap.height();
    int target_width = image.width();
    int target_height = static_cast<int>(target_width / aspect);
    if (target_height > image.height())
    {
        target_height = image.height();
        target_width = static_cast<int>(target_height * aspect);
    }
    const QRect target(
        (image.width() - target_width) / 2,
        (image.height() - target_height) / 2,
        target_width,
        target_height);
    painter.drawImage(target, bitmap);
}

class PlaylistItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QStyleOptionViewItem item_option(option);
        initStyleOption(&item_option, index);
        const QString title = item_option.text;
        item_option.text.clear();
        const QWidget* widget = item_option.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &item_option, painter, widget);

        const QRect text_rect = style->subElementRect(
            QStyle::SE_ItemViewItemText, &item_option, widget);
        const QColor text_color = item_option.state & QStyle::State_Selected
            ? item_option.palette.color(QPalette::HighlightedText)
            : item_option.palette.color(QPalette::Text);
        painter->save();
        painter->setPen(text_color);
        const QFontMetrics metrics(item_option.font);
        const QString duration = index.data(Qt::UserRole + 1).toString();
        const QString resolution = index.data(Qt::UserRole + 2).toString();
        const QString metadata = QStringLiteral("%1  %2").arg(duration, resolution);
        QFont metadata_font = item_option.font;
        metadata_font.setPointSize(std::max(8, metadata_font.pointSize() - 1));
        const QFontMetrics metadata_metrics(metadata_font);
        const int metadata_width = std::min(
            text_rect.width() / 2,
            metadata_metrics.horizontalAdvance(metadata) + 8);
        const QRect title_rect(
            text_rect.left(), text_rect.top(),
            text_rect.width() - metadata_width, text_rect.height());
        const QRect metadata_rect(
            title_rect.right() + 4, text_rect.top(),
            metadata_width, text_rect.height());
        painter->drawText(title_rect, Qt::AlignVCenter | Qt::AlignLeft,
            metrics.elidedText(title, Qt::ElideRight, title_rect.width()));
        painter->setFont(metadata_font);
        painter->setPen(item_option.palette.color(QPalette::Mid));
        painter->drawText(metadata_rect, Qt::AlignVCenter | Qt::AlignRight,
            metadata_metrics.elidedText(metadata, Qt::ElideLeft, metadata_rect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), 32));
        return size;
    }
};

QWidget* create_playlist_header(QWidget* parent)
{
    auto* header = new QWidget(parent);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(6, 2, 6, 2);
    auto* title = new QLabel(QStringLiteral("文件名"), header);
    auto* metadata = new QLabel(QStringLiteral("时长  ·  分辨率"), header);
    metadata->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(title, 1);
    layout->addWidget(metadata);
    header->setStyleSheet(QStringLiteral("QWidget { color: palette(mid); font-size: 11px; }"));
    return header;
}

QStringList expand_media_urls(
    const QList<QUrl>& urls,
    const std::shared_ptr<UiTaskTracker>& tracker)
{
    const QStringList video_filters = {
        QStringLiteral("*.mp4"), QStringLiteral("*.mkv"), QStringLiteral("*.mov"),
        QStringLiteral("*.avi"), QStringLiteral("*.webm"), QStringLiteral("*.wmv"),
        QStringLiteral("*.flv"), QStringLiteral("*.mpeg"), QStringLiteral("*.mpg"),
        QStringLiteral("*.m4v"), QStringLiteral("*.ts"), QStringLiteral("*.mts"),
        QStringLiteral("*.m2ts"), QStringLiteral("*.3gp"), QStringLiteral("*.3g2"),
        QStringLiteral("*.vob"), QStringLiteral("*.ogv"), QStringLiteral("*.mxf"),
        QStringLiteral("*.asf"), QStringLiteral("*.f4v"), QStringLiteral("*.divx"),
        QStringLiteral("*.mpe"), QStringLiteral("*.mpv")
    };
    QStringList paths;
    QSet<QString> unique_paths;
    for (const QUrl& url : urls)
    {
        if (!tracker->accepting())
            return paths;
        const QString path = url.toLocalFile();
        if (path.isEmpty())
            continue;
        const QFileInfo file_info(path);
        if (file_info.isDir())
        {
            QDirIterator iterator(
                file_info.absoluteFilePath(), video_filters,
                QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (iterator.hasNext() && tracker->accepting())
            {
                const QString candidate = QFileInfo(iterator.next()).absoluteFilePath();
                const QString key = candidate.toCaseFolded();
                if (!unique_paths.contains(key))
                {
                    unique_paths.insert(key);
                    paths.append(candidate);
                }
            }
        }
        else if (file_info.isFile())
        {
            const QString candidate = file_info.absoluteFilePath();
            const QString key = candidate.toCaseFolded();
            if (!unique_paths.contains(key))
            {
                unique_paths.insert(key);
                paths.append(candidate);
            }
        }
    }
    if (!tracker->accepting())
        return paths;
    std::sort(paths.begin(), paths.end(), [](const QString& left, const QString& right)
    {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return paths;
}

}

MainWindow::MainWindow(
    PlayerMode mode, const QStringList& initial_media_files, QWidget* parent)
    : QMainWindow(parent)
    , mode_(mode)
    , status_label_(new QLabel(this))
    , position_label_(new QLabel(QStringLiteral("00:00 / --:--"), this))
    , media_title_label_(new QLabel(QStringLiteral("未打开媒体"), this))
    , library_search_(new QLineEdit(this))
    , library_list_(new QListWidget(this))
    , seek_slider_(new SeekSlider(this))
    , volume_slider_(new QSlider(Qt::Horizontal, this))
    , play_button_(new QPushButton(QStringLiteral("播放"), this))
    , stop_button_(new QPushButton(QStringLiteral("停止"), this))
    , task_tracker_(std::make_shared<UiTaskTracker>())
    , dispatch_executor_(asio::make_strand(UiTaskRuntime::instance().io_context()))
    , background_executor_(UiTaskRuntime::instance().thread_pool_executor())
    , command_executor_(asio::make_strand(background_executor_))
    , frame_executor_(asio::make_strand(background_executor_))
    , library_executor_(asio::make_strand(background_executor_))
    , snapshot_timer_(new QTimer(this))
    , video_timer_(new QTimer(this))
    , device_timer_(new QTimer(this))
    , fullscreen_hide_timer_(new QTimer(this))
    , osd_timer_(new QTimer(this))
    , volume_settings_timer_(new QTimer(this))
{
    initialize_settings_file();
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        playback_rate_ = std::clamp(
            settings.value(QStringLiteral("Video/PlaybackRate"), 1.0).toFloat(),
            0.25f, 4.0f);
        playlist_panel_visible_ = settings.value(
            QStringLiteral("Video/UI/PlaylistVisible"), true).toBool();
        fullscreen_hide_delay_ms_ = std::clamp(
            settings.value(QStringLiteral("Video/UI/FullscreenHideDelayMs"), 2000).toInt(),
            0, 10000);
        osd_duration_ms_ = std::clamp(
            settings.value(QStringLiteral("Video/UI/OSDDurationMs"), 1500).toInt(),
            500, 5000);
        osd_position_ = std::clamp(
            settings.value(QStringLiteral("Video/UI/OSDPosition"), 1).toInt(), 0, 2);
        screenshot_directory_ = settings.value(
            QStringLiteral("Video/Screenshot/Directory"),
            QDir(application_data_directory()).filePath(QStringLiteral("Screenshots"))).toString();
        screenshot_format_ = settings.value(
            QStringLiteral("Video/Screenshot/Format"), QStringLiteral("png")).toString();
        if (screenshot_format_ != QStringLiteral("jpg") &&
            screenshot_format_ != QStringLiteral("bmp"))
            screenshot_format_ = QStringLiteral("png");
        subtitle_font_family_ = settings.value(
            QStringLiteral("Video/Subtitle/FontFamily"), QStringLiteral("Segoe UI")).toString();
        subtitle_color_ = settings.value(
            QStringLiteral("Video/Subtitle/Color"), QStringLiteral("#ffffff")).toString();
        subtitle_font_size_ = std::clamp(
            settings.value(QStringLiteral("Video/Subtitle/FontSize"), 0).toInt(), 0, 96);
        subtitle_vertical_position_ = std::clamp(
            settings.value(QStringLiteral("Video/Subtitle/VerticalPosition"), 80).toInt(), 60, 80);
        middle_click_action_ = settings.value(
            QStringLiteral("Video/Mouse/MiddleClickAction"), 2).toInt();
        double_click_action_ = settings.value(
            QStringLiteral("Video/Mouse/DoubleClickAction"), 1).toInt();
        if (settings.value(
            QStringLiteral("Video/Mouse/InteractionDefaultsVersion"), 0).toInt() < 1)
        {
            middle_click_action_ = 2;
            double_click_action_ = 1;
            settings.setValue(QStringLiteral("Video/Mouse/MiddleClickAction"), middle_click_action_);
            settings.setValue(QStringLiteral("Video/Mouse/DoubleClickAction"), double_click_action_);
            settings.setValue(QStringLiteral("Video/Mouse/InteractionDefaultsVersion"), 1);
            settings.sync();
        }
        wheel_action_ = std::clamp(
            settings.value(QStringLiteral("Video/Mouse/WheelAction"), 0).toInt(), 0, 2);
        mouse_gestures_enabled_ = settings.value(
            QStringLiteral("Video/Mouse/RightDragGestures"), false).toBool();
        auto_match_subtitle_ = settings.value(
            QStringLiteral("Video/Subtitle/AutoMatch"), true).toBool();
        subtitles_enabled_ = settings.value(
            QStringLiteral("Video/Subtitle/Enabled"), true).toBool();
        remember_position_ = settings.value(
            QStringLiteral("Video/Playback/RememberPosition"), true).toBool();
        auto_play_next_ = settings.value(
            QStringLiteral("Video/Playback/AutoPlayNext"), true).toBool();
        always_on_top_ = settings.value(
            QStringLiteral("Video/Window/AlwaysOnTop"), false).toBool();
        subtitle_offset_ms_ = settings.value(
            QStringLiteral("Video/Subtitle/OffsetMs"), 0).toLongLong();
    }
    setAcceptDrops(true);
    resize(mode_ == PlayerMode::Video ? QSize(1180, 720) : QSize(720, 480));
    setWindowTitle(mode_ == PlayerMode::Video
        ? QStringLiteral("LNPlayer - Video")
        : QStringLiteral("LNPlayer - Audio"));
    if (mode_ == PlayerMode::Video)
        setWindowFlag(Qt::FramelessWindowHint);
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        const QByteArray geometry = settings.value(QStringLiteral("Video/Window/Geometry")).toByteArray();
        if (!geometry.isEmpty())
            restoreGeometry(geometry);
        setWindowFlag(Qt::WindowStaysOnTopHint, always_on_top_);
        enableWindowResizeFrame();
        normal_geometry_ = saveGeometry();
    }

    setStyleSheet(QStringLiteral(
        "QMainWindow, QDialog { background: #171a20; color: #e8eaf0; }"
        "QMainWindow { border: 1px solid #343a46; }"
        "QWidget#windowTitleBar { background: #191d24; border-bottom: 1px solid #292f39; }"
        "QWidget#windowTitleBar QPushButton { background: transparent; border: 0; "
        "border-radius: 0; padding: 0; min-width: 46px; max-width: 46px; min-height: 36px; max-height: 36px; }"
        "QWidget#windowTitleBar QPushButton:hover { background: transparent; }"
        "QWidget#windowTitleBar QPushButton#closeWindowButton:hover { background: transparent; }"
        "QWidget { color: #e8eaf0; }"
        "QMenuBar { background: #20242c; border-bottom: 1px solid #343a46; }"
        "QMenuBar::item { padding: 6px 10px; background: transparent; }"
        "QMenuBar::item:selected { background: #343b48; }"
        "QMenu { background: #252a33; border: 1px solid #424957; padding: 4px; }"
        "QMenu::item { padding: 6px 28px 6px 12px; }"
        "QMenu::item:selected { background: #3c4655; }"
        "QPushButton { background: #2a303a; border: 1px solid #414957; "
        "border-radius: 5px; padding: 6px 12px; }"
        "QPushButton:hover { background: #384252; }"
        "QPushButton:pressed { background: #202630; }"
        "QPushButton:checked { background: #345b86; border-color: #4f8cc8; }"
        "QPushButton:disabled { color: #777f8c; background: #22262d; }"
        "QPushButton[iconOnly=\"true\"]:hover, QPushButton[iconOnly=\"true\"]:pressed, QPushButton[iconOnly=\"true\"]:checked { background: transparent; border-color: transparent; }"
        "QListWidget, QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { "
        "background: #20252d; border: 1px solid #3b4350; border-radius: 4px; "
        "selection-background-color: #345b86; padding: 4px; }"
        "QListWidget::item { padding: 5px; }"
        "QListWidget::item:selected { background: #345b86; }"
        "QSlider::groove:horizontal { height: 4px; background: #424a56; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #4d91cf; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -5px 0; "
        "background: #e4edf7; border: 1px solid #6c9bc5; border-radius: 6px; }"
        "QWidget#controlPanel { background: #151d29; border: none; border-top: 1px solid #354253; border-radius: 0; }"
        "QWidget#controlPanel QPushButton { min-height: 30px; padding: 2px; color: #f0f4fa; background: rgba(255,255,255,5); border: 1px solid transparent; border-radius: 10px; }"
        "QWidget#controlPanel QPushButton:hover { color: #8fc7ff; background: transparent; border-color: transparent; }"
        "QWidget#controlPanel QPushButton:pressed { background: transparent; border-color: transparent; }"
        "QWidget#controlPanel QPushButton:checked { color: #a9d5ff; background: transparent; border-color: transparent; }"
        "QWidget#controlPanel QPushButton:disabled { color: rgba(220,228,238,90); }"
        "QWidget#controlPanel QPushButton::menu-indicator { image: none; width: 0px; }"
        "QWidget#controlPanel QSlider::groove:horizontal { height: 3px; background: rgba(230,238,248,85); border-radius: 1px; }"
        "QWidget#controlPanel QSlider::sub-page:horizontal { background: #78b8f0; border-radius: 1px; }"
        "QWidget#controlPanel QSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; background: #f2f6fb; border: 0; border-radius: 5px; }"
        "QSplitter::handle { background: #303743; }"
        "QSplitter::handle:hover { background: #4d91cf; }"
        "QTabWidget::pane { border: 1px solid #3b4350; }"
        "QTabBar::tab { background: #252a33; padding: 7px 14px; }"
        "QTabBar::tab:selected { background: #345b86; }"
        "QStatusBar { background: #20242c; border-top: 1px solid #343a46; }"));

    if (mode_ == PlayerMode::Video)
        statusBar()->hide();

    auto* file_menu = menuBar()->addMenu(QStringLiteral("文件"));
    auto* open_action = file_menu->addAction(PlayerIcons::make(PlayerIcons::Name::OpenFile), QStringLiteral("打开文件..."));
    connect(open_action, &QAction::triggered, this, [this]
    {
        openFile();
    });
    if (mode_ == PlayerMode::Video)
    {
        auto* open_folder_action = file_menu->addAction(PlayerIcons::make(PlayerIcons::Name::OpenFolder), QStringLiteral("打开文件夹..."));
        connect(open_folder_action, &QAction::triggered, this, [this]
        {
            const QString directory = QFileDialog::getExistingDirectory(
                this, QStringLiteral("打开视频文件夹"));
            if (directory.isEmpty())
                return;

            const QStringList filters = {
                QStringLiteral("*.mp4"), QStringLiteral("*.mkv"), QStringLiteral("*.mov"),
                QStringLiteral("*.avi"), QStringLiteral("*.webm"), QStringLiteral("*.wmv"),
                QStringLiteral("*.flv"), QStringLiteral("*.mpeg"), QStringLiteral("*.mpg"),
                QStringLiteral("*.m4v"), QStringLiteral("*.ts"), QStringLiteral("*.mts"),
                QStringLiteral("*.m2ts"), QStringLiteral("*.3gp"), QStringLiteral("*.3g2"),
                QStringLiteral("*.vob"), QStringLiteral("*.ogv"), QStringLiteral("*.mxf"),
                QStringLiteral("*.asf"), QStringLiteral("*.f4v"), QStringLiteral("*.divx"),
                QStringLiteral("*.mpe"), QStringLiteral("*.mpv")
            };
            QStringList paths;
            QDirIterator iterator(
                directory, filters, QDir::Files | QDir::Readable,
                QDirIterator::Subdirectories);
            while (iterator.hasNext())
                paths.append(QFileInfo(iterator.next()).absoluteFilePath());
            std::sort(paths.begin(), paths.end(), [](const QString& left, const QString& right)
            {
                return left.compare(right, Qt::CaseInsensitive) < 0;
            });

            QList<QUrl> urls;
            for (const QString& path : paths)
                urls.append(QUrl::fromLocalFile(path));
            if (!urls.isEmpty())
                openFiles(urls);
            else
                statusBar()->showMessage(QStringLiteral("文件夹及其子目录中没有支持的视频文件"));
        });
        auto* recent_menu = file_menu->addMenu(QStringLiteral("最近播放"));
        connect(recent_menu, &QMenu::aboutToShow, this, [this, recent_menu]
        {
            recent_menu->clear();
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            const QStringList recent_files = settings.value(
                QStringLiteral("Video/RecentFiles")).toStringList();
            for (const QString& path : recent_files)
            {
                if (!QFileInfo::exists(path))
                    continue;
                QAction* action = recent_menu->addAction(QFileInfo(path).fileName());
                action->setToolTip(path);
                connect(action, &QAction::triggered, this, [this, path]
                {
                    openFiles({QUrl::fromLocalFile(path)});
                });
            }
            if (recent_menu->actions().isEmpty())
            {
                QAction* empty = recent_menu->addAction(QStringLiteral("暂无记录"));
                empty->setEnabled(false);
            }
        });
    }
    if (mode_ == PlayerMode::Video)
    {
        auto* subtitle_action = file_menu->addAction(PlayerIcons::make(PlayerIcons::Name::Subtitles), QStringLiteral("加载外挂字幕..."));
        connect(subtitle_action, &QAction::triggered, this, [this]
        {
            loadExternalSubtitle();
        });
    }
    if (mode_ == PlayerMode::Audio)
    {
        auto* import_action = file_menu->addAction(QStringLiteral("导入音乐目录..."));
        connect(import_action, &QAction::triggered, this, [this]
        {
            const QString directory = QFileDialog::getExistingDirectory(
                this, QStringLiteral("选择音乐目录"));
            if (directory.isEmpty())
                return;

            lnplayer_t* player_handle = player_.load(std::memory_order_acquire);

            if (!player_handle)
                return;

            const QByteArray path_utf8 = directory.toUtf8();
            status_label_->setText(QStringLiteral("正在扫描音乐目录..."));
            postLibrary([this, path_utf8, player_handle]
            {
                int imported_count = 0;
                const lnplayer_result_t result = lnplayer_library_scan_directory(
                    player_handle, path_utf8.constData(), 1, &imported_count);

                QMetaObject::invokeMethod(this, [this, result, imported_count]
                {
                    if (result == LNPLAYER_RESULT_OK)
                    {
                        status_label_->setText(
                            QStringLiteral("目录扫描完成，导入 %1 个音频文件").arg(imported_count));
                        statusBar()->showMessage(QStringLiteral("音乐库已更新"));
                        refreshLibrary();
                    }
                    else
                    {
                        status_label_->setText(QStringLiteral("音乐目录扫描失败"));
                    }
                }, Qt::QueuedConnection);
            });
        });
    }

    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction(QStringLiteral("退出"));
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    if (mode_ == PlayerMode::Video)
    {
        auto* playback_menu = menuBar()->addMenu(QStringLiteral("播放"));
        auto* play_pause_action = playback_menu->addAction(QStringLiteral("播放/暂停"));
        connect(play_pause_action, &QAction::triggered, this, [this] { togglePlayback(); });
        auto* stop_action = playback_menu->addAction(QStringLiteral("停止"));
        connect(stop_action, &QAction::triggered, this, [this]
        {
            postCommand([this]
            {
                if (player_)
                    lnplayer_stop(player_);
            });
        });
        playback_menu->addSeparator();
        auto* previous_action = playback_menu->addAction(QStringLiteral("上一项"));
        auto* next_action = playback_menu->addAction(QStringLiteral("下一项"));
        connect(previous_action, &QAction::triggered, this, [this] { playPrevious(); });
        connect(next_action, &QAction::triggered, this, [this] { playNext(); });

        auto* bookmarks_menu = playback_menu->addMenu(QStringLiteral("书签"));
        connect(bookmarks_menu, &QMenu::aboutToShow, this, [this, bookmarks_menu]
        {
            bookmarks_menu->clear();
            if (current_media_path_.isEmpty())
            {
                QAction* unavailable = bookmarks_menu->addAction(QStringLiteral("请先打开视频"));
                unavailable->setEnabled(false);
                return;
            }

            QSettings settings(settings_file_path(), QSettings::IniFormat);
            const QString key = mediaPreferenceKey("Bookmarks", current_media_path_);
            const QVariantList bookmarks = settings.value(key).toList();
            QAction* add_action = bookmarks_menu->addAction(QStringLiteral("添加当前书签..."));
            connect(add_action, &QAction::triggered, this, [this, key, bookmark_count = bookmarks.size()]
            {
                const int64_t bookmark_position = current_position_ms_;
                bool accepted = false;
                const QString default_name = QStringLiteral("书签 %1").arg(bookmark_count + 1);
                const QString name = QInputDialog::getText(
                    this, QStringLiteral("添加书签"), QStringLiteral("名称"),
                    QLineEdit::Normal,
                    QStringLiteral("%1 · %2").arg(default_name, format_position(bookmark_position)),
                    &accepted).trimmed();
                if (!accepted || name.isEmpty())
                    return;

                QSettings store(settings_file_path(), QSettings::IniFormat);
                QVariantList updated = store.value(key).toList();
                QVariantMap entry;
                entry.insert(QStringLiteral("position"), bookmark_position);
                entry.insert(QStringLiteral("name"), name);
                updated.append(entry);
                store.setValue(key, updated);
                store.sync();
                statusBar()->showMessage(QStringLiteral("书签已添加：%1").arg(name));
            });

            bookmarks_menu->addSeparator();
            for (int index = 0; index < bookmarks.size(); ++index)
            {
                const QVariantMap entry = bookmarks[index].toMap();
                const int64_t position = entry.value(QStringLiteral("position")).toLongLong();
                const QString name = entry.value(QStringLiteral("name")).toString();
                auto* bookmark_menu = bookmarks_menu->addMenu(
                    QStringLiteral("%1  %2").arg(name, format_position(position)));
                auto* jump_action = bookmark_menu->addAction(QStringLiteral("跳转"));
                connect(jump_action, &QAction::triggered, this, [this, position, name]
                {
                    seekToPosition(position, QStringLiteral("跳转书签：%1").arg(name));
                });
                auto* remove_action = bookmark_menu->addAction(QStringLiteral("删除"));
                connect(remove_action, &QAction::triggered, this, [this, key, index]
                {
                    QSettings store(settings_file_path(), QSettings::IniFormat);
                    QVariantList updated = store.value(key).toList();
                    if (index >= 0 && index < updated.size())
                    {
                        updated.removeAt(index);
                        store.setValue(key, updated);
                        store.sync();
                    }
                });
            }
            if (bookmarks.isEmpty())
            {
                QAction* empty = bookmarks_menu->addAction(QStringLiteral("暂无书签"));
                empty->setEnabled(false);
            }
            else
            {
                bookmarks_menu->addSeparator();
                auto* clear_action = bookmarks_menu->addAction(QStringLiteral("清空本视频书签"));
                connect(clear_action, &QAction::triggered, this, [this, key]
                {
                    QSettings store(settings_file_path(), QSettings::IniFormat);
                    store.remove(key);
                    store.sync();
                });
            }
        });

        auto* chapter_menu = playback_menu->addMenu(QStringLiteral("章节"));
        connect(chapter_menu, &QMenu::aboutToShow, this, [this, chapter_menu]
        {
            chapter_menu->clear();
            struct ChapterEntry
            {
                int64_t start_ms;
                QString title;
            };
            std::vector<ChapterEntry> chapters;
            if (player_)
            {
                lnplayer_enumerate_chapters(
                    player_,
                    [](const lnplayer_chapter_info_t* chapter, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<ChapterEntry>*>(user_data);
                        entries->push_back({
                            chapter->start_ms,
                            QString::fromUtf8(chapter->title_utf8
                                ? chapter->title_utf8 : "")});
                    },
                    &chapters);
            }

            for (const ChapterEntry& chapter : chapters)
            {
                const QString label = QStringLiteral("%1  %2")
                    .arg(format_position(chapter.start_ms), chapter.title);
                QAction* action = chapter_menu->addAction(label);
                connect(action, &QAction::triggered, this, [this, chapter]
                {
                    seekToPosition(chapter.start_ms,
                        QStringLiteral("跳转章节：%1").arg(chapter.title));
                });
            }
            if (chapters.empty())
            {
                QAction* unavailable = chapter_menu->addAction(QStringLiteral("当前文件没有章节"));
                unavailable->setEnabled(false);
            }
        });

        auto* speed_menu = playback_menu->addMenu(QStringLiteral("播放速度"));
        QList<QAction*> speed_actions;
        const float playback_speeds[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f};
        auto* speed_group = new QActionGroup(speed_menu);
        speed_group->setExclusive(true);
        for (float speed : playback_speeds)
        {
            QAction* action = speed_menu->addAction(
                QStringLiteral("%1x").arg(speed, 0, 'f', 2));
            action->setCheckable(true);
            action->setData(speed);
            speed_group->addAction(action);
            speed_actions.append(action);
            connect(action, &QAction::triggered, this, [this, speed]
            {
                setPlaybackRate(speed);
            });
        }
        connect(speed_menu, &QMenu::aboutToShow, this, [this, speed_actions]
        {
            for (QAction* action : speed_actions)
                action->setChecked(std::abs(action->data().toFloat() - playback_rate_) < 0.001f);
        });

        auto* audio_menu = menuBar()->addMenu(QStringLiteral("音轨"));
        connect(audio_menu, &QMenu::aboutToShow, this, [this, audio_menu]
        {
            audio_menu->clear();
            std::vector<std::pair<int, QString>> tracks;
            if (player_)
            {
                lnplayer_enumerate_audio_tracks(
                    player_,
                    [](const lnplayer_audio_track_info_t* track, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<std::pair<int, QString>>*>(user_data);
                        QString label = track->title_utf8 && track->title_utf8[0]
                            ? QString::fromUtf8(track->title_utf8)
                            : QString::fromUtf8(track->language_utf8 ? track->language_utf8 : "");
                        if (label.isEmpty())
                            label = QString::fromUtf8(track->codec_utf8 ? track->codec_utf8 : "Audio");
                        if (track->channels > 0)
                            label += QStringLiteral(" (%1 ch)").arg(track->channels);
                        if (track->is_default)
                            label.prepend(QStringLiteral("[默认] "));
                        if (track->is_selected)
                            label.prepend(QStringLiteral("✓ "));
                        entries->emplace_back(track->stream_index, label);
                    },
                    &tracks);
            }
            for (const auto& track : tracks)
            {
                QAction* action = audio_menu->addAction(track.second);
                action->setData(track.first);
                connect(action, &QAction::triggered, this, [this, index = track.first]
                {
                    selectAudioTrack(index);
                });
            }
            if (tracks.empty())
            {
                QAction* unavailable = audio_menu->addAction(QStringLiteral("无可用音轨"));
                unavailable->setEnabled(false);
            }
        });

        auto* subtitle_menu = menuBar()->addMenu(QStringLiteral("字幕"));
        auto* embedded_tracks_menu = subtitle_menu->addMenu(QStringLiteral("内嵌字幕"));
        connect(embedded_tracks_menu, &QMenu::aboutToShow, this, [this, embedded_tracks_menu]
        {
            embedded_tracks_menu->clear();
            QAction* disable_action = embedded_tracks_menu->addAction(QStringLiteral("禁用内嵌字幕"));
            connect(disable_action, &QAction::triggered, this, [this]
            {
                selectSubtitleTrack(-1);
            });
            embedded_tracks_menu->addSeparator();
            struct TrackEntry
            {
                int index;
                QString label;
            };
            std::vector<TrackEntry> tracks;
            if (player_)
            {
                lnplayer_enumerate_subtitle_tracks(
                    player_,
                    [](const lnplayer_subtitle_track_info_t* track, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<TrackEntry>*>(user_data);
                        QString label = track->title_utf8 && track->title_utf8[0]
                            ? QString::fromUtf8(track->title_utf8)
                            : QString::fromUtf8(track->language_utf8 ? track->language_utf8 : "");
                        if (label.isEmpty())
                            label = QString::fromUtf8(track->codec_utf8 ? track->codec_utf8 : "Subtitle");
                        if (track->is_default)
                            label.prepend(QStringLiteral("[默认] "));
                        if (track->is_selected)
                            label.prepend(QStringLiteral("✓ "));
                        entries->push_back({track->stream_index, label});
                    },
                    &tracks);
            }
            for (const auto& track : tracks)
            {
                QAction* action = embedded_tracks_menu->addAction(track.label);
                connect(action, &QAction::triggered, this, [this, index = track.index]
                {
                    selectSubtitleTrack(index);
                });
            }
            if (tracks.empty())
            {
                QAction* unavailable = embedded_tracks_menu->addAction(QStringLiteral("无可用内嵌字幕"));
                unavailable->setEnabled(false);
            }
        });
        auto* load_subtitle_action = subtitle_menu->addAction(PlayerIcons::make(PlayerIcons::Name::Subtitles), QStringLiteral("加载外挂字幕..."));
        connect(load_subtitle_action, &QAction::triggered, this, [this] { loadExternalSubtitle(); });
        auto* subtitles_enabled_action = subtitle_menu->addAction(QStringLiteral("启用字幕"));
        subtitles_enabled_action->setCheckable(true);
        subtitles_enabled_action->setChecked(subtitles_enabled_);
        connect(subtitle_menu, &QMenu::aboutToShow, this, [this, subtitles_enabled_action]
        {
            subtitles_enabled_action->setChecked(subtitles_enabled_);
        });
        connect(subtitles_enabled_action, &QAction::toggled, this, [this](bool enabled)
        {
            subtitles_enabled_ = enabled;
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Video/Subtitle/Enabled"), enabled);
            settings.sync();
            postCommand([this, enabled]
            {
                if (player_)
                    lnplayer_set_subtitles_enabled(player_, enabled ? 1 : 0);
            });
            pending_video_frame_valid_ = false;
            requestVideoFrame();
            showOsd(enabled ? QStringLiteral("字幕已启用") : QStringLiteral("字幕已关闭"));
        });
        subtitle_menu->addSeparator();
        auto* subtitle_delay_action = subtitle_menu->addAction(QStringLiteral("字幕延迟 +500 ms"));
        auto* subtitle_advance_action = subtitle_menu->addAction(QStringLiteral("字幕提前 500 ms"));
        auto* subtitle_reset_action = subtitle_menu->addAction(QStringLiteral("重置字幕时间"));
        connect(subtitle_delay_action, &QAction::triggered, this,
            [this] { setSubtitleOffset(subtitle_offset_ms_ + 500); });
        connect(subtitle_advance_action, &QAction::triggered, this,
            [this] { setSubtitleOffset(subtitle_offset_ms_ - 500); });
        connect(subtitle_reset_action, &QAction::triggered, this,
            [this] { setSubtitleOffset(0); });
    }

    if (mode_ == PlayerMode::Video)
    {
        auto* video_menu = menuBar()->addMenu(QStringLiteral("视频"));
        auto* playback_info_action = video_menu->addAction(QStringLiteral("播放信息..."));
        connect(playback_info_action, &QAction::triggered, this, [this] { showPlaybackInfo(); });
        auto* screenshot_action = video_menu->addAction(QStringLiteral("截图..."));
        connect(screenshot_action, &QAction::triggered, this, [this] { takeScreenshot(); });
        always_on_top_action_ = video_menu->addAction(QStringLiteral("窗口置顶"));
        always_on_top_action_->setCheckable(true);
        always_on_top_action_->setChecked(always_on_top_);
        connect(always_on_top_action_, &QAction::toggled, this, [this](bool enabled)
        {
            always_on_top_ = enabled;
            const bool was_fullscreen = isFullScreen();
            const bool was_maximized = isMaximized();
            setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
            enableWindowResizeFrame();
            if (was_fullscreen)
                showFullScreen();
            else if (was_maximized)
                showMaximized();
            else
                show();
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Video/Window/AlwaysOnTop"), enabled);
            settings.sync();
        });
    }

    if (mode_ == PlayerMode::Video)
    {
        auto* playlist_menu = menuBar()->addMenu(QStringLiteral("播放列表"));
        playlist_visibility_action_ = playlist_menu->addAction(QStringLiteral("显示播放列表"));
        playlist_visibility_action_->setCheckable(true);
        playlist_visibility_action_->setChecked(playlist_panel_visible_);
        connect(playlist_visibility_action_, &QAction::toggled, this, [this](bool visible)
        {
            playlist_panel_visible_ = visible;
            if (playlist_panel_ && !visible)
                playlist_panel_->hide();
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Video/UI/PlaylistVisible"), visible);
            settings.sync();
        });
        auto* clear_playlist_action = playlist_menu->addAction(QStringLiteral("清空播放列表"));
        connect(clear_playlist_action, &QAction::triggered, this, [this]
        {
            clearPlaylist();
        });
        playlist_menu->addSeparator();
        auto* detached_playlist_action =
            playlist_menu->addAction(QStringLiteral("在独立窗口中打开播放列表..."));
        connect(detached_playlist_action, &QAction::triggered,
            this, [this] { showPlaylistWindow(); });
    }

    if (mode_ == PlayerMode::Video)
    {
        auto* settings_menu = menuBar()->addMenu(QStringLiteral("设置"));
        auto* video_settings_action = settings_menu->addAction(QStringLiteral("播放器设置..."));
        connect(video_settings_action, &QAction::triggered, this, [this] { openVideoSettings(); });
    }

    if (mode_ == PlayerMode::Video)
    {
        title_bar_ = new QWidget(this);
        title_bar_->setObjectName(QStringLiteral("windowTitleBar"));
        title_bar_->setFixedHeight(36);
        auto* title_layout = new QHBoxLayout(title_bar_);
        title_layout->setContentsMargins(12, 0, 0, 0);
        title_layout->setSpacing(0);
        auto* application_icon = new QLabel(title_bar_);
        application_icon->setFixedSize(24, 24);
        application_icon->setPixmap(PlayerIcons::make(PlayerIcons::Name::AppLogo)
            .pixmap(QSize(24, 24)));
        title_layout->addWidget(application_icon);
        title_layout->addSpacing(9);
        auto* title_label = new QLabel(windowTitle(), title_bar_);
        QFont title_font = title_label->font();
        title_font.setPointSize(10);
        title_font.setWeight(QFont::DemiBold);
        title_label->setFont(title_font);
        title_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        title_layout->addWidget(title_label, 1);
        auto* minimize_button = new QPushButton(title_bar_);
    minimize_button->setProperty("iconOnly", true);
        minimize_button->setIcon(PlayerIcons::make(PlayerIcons::Name::Minimize, QColor(QStringLiteral("#9aa6b5"))));
        minimize_button->setIconSize(QSize(18, 18));
        minimize_button->setFixedSize(46, 36);
        minimize_button->setToolTip(QStringLiteral("最小化"));
        minimize_button->setAccessibleName(QStringLiteral("最小化"));
        title_layout->addWidget(minimize_button);
        auto* maximize_button = new QPushButton(title_bar_);
    maximize_button->setProperty("iconOnly", true);
        maximize_button->setObjectName(QStringLiteral("maximizeWindowButton"));
        maximize_button->setIcon(PlayerIcons::make(PlayerIcons::Name::Maximize, QColor(QStringLiteral("#9aa6b5"))));
        maximize_button->setIconSize(QSize(18, 18));
        maximize_button->setFixedSize(46, 36);
        maximize_button->setToolTip(QStringLiteral("最大化"));
        maximize_button->setAccessibleName(QStringLiteral("最大化"));
        title_layout->addWidget(maximize_button);
        auto* close_button = new QPushButton(title_bar_);
    close_button->setProperty("iconOnly", true);
        close_button->setObjectName(QStringLiteral("closeWindowButton"));
        close_button->setIcon(PlayerIcons::make(PlayerIcons::Name::Close, QColor(QStringLiteral("#9aa6b5"))));
        close_button->setIconSize(QSize(18, 18));
        close_button->setFixedSize(46, 36);
        close_button->setToolTip(QStringLiteral("关闭"));
        close_button->setAccessibleName(QStringLiteral("关闭"));
        title_layout->addWidget(close_button);
        connect(minimize_button, &QPushButton::clicked, this, &QWidget::showMinimized);
        connect(maximize_button, &QPushButton::clicked, this, [this]
        {
            if (isMaximized())
                showNormal();
            else
                showMaximized();
        });
        connect(close_button, &QPushButton::clicked, this, &QWidget::close);
        QMenuBar* original_menu_bar = menuBar();
        menu_bar_ = new QMenuBar(this);
        for (QAction* action : original_menu_bar->actions())
        {
            if (QMenu* menu = action->menu())
            {
                original_menu_bar->removeAction(action);
                menu->setParent(menu_bar_);
                menu_bar_->addMenu(menu);
            }
            else
            {
                original_menu_bar->removeAction(action);
                menu_bar_->addAction(action);
            }
        }
        original_menu_bar->hide();
        menu_bar_->hide();
        menu_bar_->installEventFilter(this);
        for (QWidget* child : menu_bar_->findChildren<QWidget*>())
        {
            child->setMouseTracking(true);
            child->installEventFilter(this);
        }
        title_bar_->setMouseTracking(true);
        title_bar_->installEventFilter(this);
        for (QWidget* child : title_bar_->findChildren<QWidget*>())
        {
            child->setMouseTracking(true);
            child->installEventFilter(this);
        }
    }

    auto* content = new QWidget(this);
    content->setObjectName(QStringLiteral("centralContent"));
    auto* layout = new QVBoxLayout(content);
    if (mode_ == PlayerMode::Video)
        layout->setContentsMargins(0, 0, 0, 0);

    if (mode_ == PlayerMode::Video)
    {
        video_surface_ = new VideoSurface(content);
        osd_label_ = new QLabel(video_surface_);
        osd_label_->setAlignment(Qt::AlignCenter);
        osd_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        osd_label_->setStyleSheet(QStringLiteral(
            "QLabel { color: white; background: rgba(0, 0, 0, 185); "
            "border-radius: 8px; padding: 10px 18px; font-size: 18px; }"));
        osd_label_->hide();
        empty_state_label_ = new QLabel(
            QStringLiteral("拖放视频文件到此处，或右键选择打开视频"),
            video_surface_);
        empty_state_label_->setAlignment(Qt::AlignCenter);
        empty_state_label_->setWordWrap(true);
        empty_state_label_->setMaximumWidth(520);
        empty_state_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        empty_state_label_->setStyleSheet(QStringLiteral(
            "QLabel { color: #c7ced8; background: rgba(0, 0, 0, 145); "
            "border-radius: 10px; padding: 16px 24px; font-size: 16px; }"));
        empty_state_label_->adjustSize();
        empty_state_label_->move(
            (video_surface_->width() - empty_state_label_->width()) / 2,
            (video_surface_->height() - empty_state_label_->height()) / 2);
        empty_state_label_->show();
        osd_timer_->setSingleShot(true);
        connect(osd_timer_, &QTimer::timeout, osd_label_, &QWidget::hide);
        {
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            video_brightness_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Brightness"), 0.0).toFloat(),
                -1.0f, 1.0f);
            video_contrast_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Contrast"), 1.0).toFloat(),
                0.0f, 2.0f);
            video_saturation_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Saturation"), 1.0).toFloat(),
                0.0f, 2.0f);
            video_hue_degrees_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/HueDegrees"), 0.0).toFloat(),
                -180.0f, 180.0f);
            video_sharpness_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Sharpness"), 0.0).toFloat(),
                0.0f, 2.0f);
            video_denoise_ = std::clamp(
                settings.value(QStringLiteral("Video/Filter/Denoise"), 0.0).toFloat(),
                0.0f, 1.0f);
            video_grayscale_ = settings.value(
                QStringLiteral("Video/Filter/Grayscale"), false).toBool();
            video_aspect_ratio_override_ = settings.value(
                QStringLiteral("Video/Display/AspectRatio"), 0.0).toFloat();
            if (video_aspect_ratio_override_ != 0.0f &&
                (video_aspect_ratio_override_ < 0.5f ||
                    video_aspect_ratio_override_ > 3.0f))
                video_aspect_ratio_override_ = 0.0f;
            video_stretch_ = settings.value(
                QStringLiteral("Video/Display/Stretch"), false).toBool();
            playlist_panel_visible_ = settings.value(
                QStringLiteral("Video/UI/PlaylistVisible"), true).toBool();
        }
        video_surface_->set_aspect_ratio(video_aspect_ratio_override_);
        video_surface_->set_stretch(video_stretch_);
        video_surface_->set_video_filter(
            video_brightness_, video_contrast_, video_saturation_,
            video_hue_degrees_, video_sharpness_, video_denoise_, video_grayscale_);
        playlist_widget_ = new QListWidget(content);
        playlist_widget_->setItemDelegate(new PlaylistItemDelegate(playlist_widget_));
        playlist_widget_->setMinimumWidth(210);
        playlist_widget_->setStyleSheet(QStringLiteral("QListWidget::item { padding: 1px 3px; }"));
        playlist_widget_->setDragDropMode(QAbstractItemView::InternalMove);
        playlist_widget_->setDropIndicatorShown(true);
        playlist_widget_->setAcceptDrops(true);
        playlist_widget_->installEventFilter(this);
        playlist_widget_->setSelectionMode(QAbstractItemView::ExtendedSelection);
        playlist_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
        playlist_widget_->setDefaultDropAction(Qt::MoveAction);
        playlist_panel_ = new QWidget(video_surface_);
        playlist_panel_->setAttribute(Qt::WA_NativeWindow);
        playlist_panel_->setAutoFillBackground(true);
        playlist_panel_->setObjectName(QStringLiteral("playlistOverlay"));
        playlist_panel_->setFixedWidth(320);
        playlist_panel_->setStyleSheet(QStringLiteral(
            "QWidget#playlistOverlay { background: #171d27; border-left: 1px solid #354253; }"));
        playlist_panel_->hide();
        auto* playlist_panel_layout = new QVBoxLayout(playlist_panel_);
        playlist_panel_layout->setContentsMargins(0, 0, 0, 0);
        playlist_panel_layout->setSpacing(3);
        auto* playlist_actions = new QHBoxLayout();
        playlist_actions->setContentsMargins(0, 0, 0, 0);
        playlist_actions->setSpacing(3);
        auto* add_to_queue = new QPushButton(playlist_panel_);
    add_to_queue->setProperty("iconOnly", true);
        add_to_queue->setIcon(PlayerIcons::make(PlayerIcons::Name::Add));
        add_to_queue->setToolTip(QStringLiteral("添加到播放列表"));
        auto* remove_from_queue = new QPushButton(playlist_panel_);
    remove_from_queue->setProperty("iconOnly", true);
        remove_from_queue->setIcon(PlayerIcons::make(PlayerIcons::Name::Remove));
        remove_from_queue->setToolTip(QStringLiteral("移除选中项"));
        auto* clear_queue = new QPushButton(playlist_panel_);
    clear_queue->setProperty("iconOnly", true);
        clear_queue->setIcon(PlayerIcons::make(PlayerIcons::Name::Clear));
        clear_queue->setToolTip(QStringLiteral("清空播放列表"));
        auto* move_up = new QPushButton(playlist_panel_);
    move_up->setProperty("iconOnly", true);
        move_up->setIcon(PlayerIcons::make(PlayerIcons::Name::MoveUp));
        move_up->setToolTip(QStringLiteral("上移"));
        auto* move_down = new QPushButton(playlist_panel_);
    move_down->setProperty("iconOnly", true);
        move_down->setIcon(PlayerIcons::make(PlayerIcons::Name::MoveDown));
        move_down->setToolTip(QStringLiteral("下移"));
        for (QPushButton* button : {add_to_queue, remove_from_queue, clear_queue, move_up, move_down})
        {
            button->setMinimumHeight(24);
            button->setMaximumHeight(26);
            button->setIconSize(QSize(16, 16));
            button->setText(QString());
            button->setMinimumWidth(0);
            button->setStyleSheet(QStringLiteral("QPushButton { padding: 2px 3px; border-radius: 3px; }"));
            playlist_actions->addWidget(button, 1);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }
        auto* shuffle_button = new QPushButton(playlist_panel_);
        auto* repeat_button = new QPushButton(playlist_panel_);
        shuffle_button_ = shuffle_button;
        repeat_button_ = repeat_button;
        shuffle_button->setCheckable(true);
        shuffle_button->setIcon(PlayerIcons::make(PlayerIcons::Name::Shuffle,
            QColor(QStringLiteral("#b7c4d5"))));
        shuffle_button->setIconSize(QSize(16, 16));
        shuffle_button->setText(QString());
        shuffle_button->setToolTip(QStringLiteral("随机播放：关"));
        shuffle_button->setAccessibleName(QStringLiteral("随机播放：关"));
        repeat_button->setIcon(PlayerIcons::make(
            PlayerIcons::Name::Repeat, QColor(QStringLiteral("#8f99a8"))));
        repeat_button->setIconSize(QSize(16, 16));
        repeat_button->setText(QString());
        repeat_button->setToolTip(QStringLiteral("循环播放：关"));
        repeat_button->setAccessibleName(QStringLiteral("循环播放：关"));
        playlist_panel_layout->addLayout(playlist_actions);
        auto* playback_modes = new QHBoxLayout();
        playback_modes->setSpacing(3);
        shuffle_button->setMinimumHeight(24);
        repeat_button->setMinimumHeight(24);
        shuffle_button->setMaximumHeight(26);
        repeat_button->setMaximumHeight(26);
        playback_modes->addWidget(shuffle_button);
        playback_modes->addWidget(repeat_button);
        playlist_panel_layout->addLayout(playback_modes);
        playlist_panel_layout->addWidget(create_playlist_header(playlist_panel_));
        playlist_panel_layout->addWidget(playlist_widget_, 1);
        connect(add_to_queue, &QPushButton::clicked, this, [this]
        {
            const QStringList files = QFileDialog::getOpenFileNames(
                this, QStringLiteral("添加到播放队列"), QString(),
                QStringLiteral("视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.m4v *.ts *.mts *.m2ts *.3gp *.3g2 *.vob *.ogv *.mxf *.asf *.f4v *.divx *.mpe *.mpv);;所有文件 (*)"));
            QList<QUrl> urls;
            for (const QString& file : files)
                urls.append(QUrl::fromLocalFile(file));
            appendPlaylistFiles(urls);
        });
        connect(remove_from_queue, &QPushButton::clicked, this,
            [this] { removeSelectedPlaylistItem(); });
        connect(clear_queue, &QPushButton::clicked, this,
            [this] { clearPlaylist(); });
        connect(move_up, &QPushButton::clicked, this,
            [this] { moveSelectedPlaylistItem(-1); });
        connect(move_down, &QPushButton::clicked, this,
            [this] { moveSelectedPlaylistItem(1); });
        connect(shuffle_button, &QPushButton::toggled, this, [this, shuffle_button](bool enabled)
        {
            shuffle_enabled_ = enabled;
            resetShuffleOrder();
            shuffle_button->setIcon(PlayerIcons::make(PlayerIcons::Name::Shuffle,
                enabled ? QColor(QStringLiteral("#f0f4fa"))
                    : QColor(QStringLiteral("#8f99a8"))));
            shuffle_button->setToolTip(enabled ? QStringLiteral("随机播放：开") : QStringLiteral("随机播放：关"));
            shuffle_button->setAccessibleName(shuffle_button->toolTip());
            saveVideoPlaylist();
        });
        connect(repeat_button, &QPushButton::clicked, this, [this, repeat_button]
        {
            cycleRepeatMode();
            repeat_button->setIcon(PlayerIcons::make(
                repeat_mode_ == 2 ? PlayerIcons::Name::RepeatOne : PlayerIcons::Name::Repeat,
                repeat_mode_ == 0 ? QColor(QStringLiteral("#8f99a8"))
                    : QColor(QStringLiteral("#d7dde7"))));
            repeat_button->setToolTip(repeat_mode_ == 0 ? QStringLiteral("循环播放：关")
                : repeat_mode_ == 1 ? QStringLiteral("循环播放：列表") : QStringLiteral("循环播放：单项"));
            repeat_button->setAccessibleName(repeat_button->toolTip());
            saveVideoPlaylist();
        });
        connect(playlist_widget_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex&, int, int, const QModelIndex&, int)
            {
                QList<QUrl> reordered;
                for (int row = 0; row < playlist_widget_->count(); ++row)
                {
                    const QString path = playlist_widget_->item(row)->data(Qt::UserRole).toString();
                    reordered.append(QUrl::fromLocalFile(path));
                }
                playlist_ = std::move(reordered);
                playlist_index_ = playing_playlist_item_.isValid()
                    ? playing_playlist_item_.row()
                    : -1;
                if (playlist_index_ >= 0)
                    playlist_widget_->setCurrentRow(playlist_index_);
                resetShuffleOrder();
            });
        connect(playlist_widget_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item)
            {
                openPlaylistIndex(playlist_widget_->row(item));
            });
        connect(playlist_widget_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& point)
        {
            QListWidgetItem* item = playlist_widget_->itemAt(point);
            if (item && !item->isSelected())
            {
                playlist_widget_->clearSelection();
                item->setSelected(true);
                playlist_widget_->setCurrentItem(item);
            }
            else if (!item)
            {
                playlist_widget_->clearSelection();
                playlist_widget_->setCurrentItem(nullptr);
            }
            QMenu menu(playlist_widget_);
            menu.setMouseTracking(true);
            menu.installEventFilter(this);
            QAction* add_files = menu.addAction(QStringLiteral("添加文件..."));
            QAction* play_selected = menu.addAction(QStringLiteral("播放"));
            play_selected->setEnabled(item != nullptr);
            QAction* remove_selected = menu.addAction(QStringLiteral("移除选中项"));
            remove_selected->setEnabled(!playlist_widget_->selectedItems().isEmpty());
            QAction* move_up = menu.addAction(QStringLiteral("上移"));
            move_up->setEnabled(item && playlist_widget_->row(item) > 0);
            QAction* move_down = menu.addAction(QStringLiteral("下移"));
            move_down->setEnabled(item &&
                playlist_widget_->row(item) < playlist_widget_->count() - 1);
            menu.addSeparator();
            QAction* clear_all = menu.addAction(QStringLiteral("清空播放列表"));
            clear_all->setEnabled(playlist_widget_->count() > 0);
            menu.addSeparator();
            QAction* settings_action = menu.addAction(QStringLiteral("播放器设置..."));
            QAction* selected = menu.exec(playlist_widget_->mapToGlobal(point));
            if (selected == add_files)
            {
                const QStringList files = QFileDialog::getOpenFileNames(
                    this, QStringLiteral("添加到播放队列"), QString(),
                    QStringLiteral("视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.m4v *.ts *.mts *.m2ts *.3gp *.3g2 *.vob *.ogv *.mxf *.asf *.f4v *.divx *.mpe *.mpv);;所有文件 (*)"));
                QList<QUrl> urls;
                for (const QString& file : files)
                    urls.append(QUrl::fromLocalFile(file));
                appendPlaylistFiles(urls);
            }
            else if (selected == play_selected && item)
                openPlaylistIndex(playlist_widget_->row(item));
            else if (selected == remove_selected)
                removeSelectedPlaylistItem();
            else if (selected == move_up)
                moveSelectedPlaylistItem(-1);
            else if (selected == move_down)
                moveSelectedPlaylistItem(1);
            else if (selected == clear_all)
                clearPlaylist();
            else if (selected == settings_action)
                openVideoSettings();
        });
        video_surface_->set_interaction_callbacks(
            [this]
            {
                if (double_click_action_ == 0)
                {
                    if (isFullScreen())
                        showNormal();
                    else
                        showFullScreen();
                }
                else if (double_click_action_ == 1)
                {
                    if (current_media_path_.isEmpty() || !has_video_)
                        openFile();
                    else
                        togglePlayback();
                }
            },
            [this](int direction, bool control)
            {
                if (control || wheel_action_ == 2)
                {
                    video_surface_->adjust_zoom(direction);
                    showOsd(QStringLiteral("画面缩放 %1%")
                        .arg(static_cast<int>(video_surface_->zoom() * 100.0f)));
                    return;
                }
                if (wheel_action_ == 1)
                {
                    seekRelative(static_cast<int64_t>(direction) * 5000);
                    return;
                }
                volume_slider_->setValue(std::clamp(
                    volume_slider_->value() + direction * 50, 0, 1000));
            },
            [this]
            {
                if (middle_click_action_ == 2)
                {
                    if (isFullScreen())
                        showNormal();
                    else
                        showFullScreen();
                }
                else if (middle_click_action_ == 1)
                {
                    togglePlayback();
                }
            },
            [this](int direction)
            {
                if (direction == 0)
                    seekRelative(-5000);
                else if (direction == 1)
                    seekRelative(5000);
                else if (direction == 2)
                    volume_slider_->setValue(std::clamp(volume_slider_->value() + 50, 0, 1000));
                else
                    volume_slider_->setValue(std::clamp(volume_slider_->value() - 50, 0, 1000));
            });
        video_surface_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(video_surface_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point)
        {
            if (video_surface_->consume_gesture_context_menu())
                return;

            QMenu menu(this);
            menu.setMouseTracking(true);
            menu.installEventFilter(this);
            QAction* open_media = menu.addAction(QStringLiteral("打开文件..."));
            QAction* open_folder = menu.addAction(QStringLiteral("打开文件夹..."));
            menu.addSeparator();
            QAction* play_pause = menu.addAction(is_playing_
                ? QStringLiteral("暂停")
                : QStringLiteral("播放"));
            QAction* stop = menu.addAction(QStringLiteral("停止"));
            menu.addSeparator();
            QAction* reset_zoom = menu.addAction(QStringLiteral("重置画面缩放"));
            QAction* fullscreen = menu.addAction(isFullScreen()
                ? QStringLiteral("退出全屏")
                : QStringLiteral("全屏"));
            QAction* screenshot = menu.addAction(QStringLiteral("截图..."));

            QMenu* audio_track_menu = menu.addMenu(QStringLiteral("音轨"));
            struct AudioTrackMenuEntry
            {
                int stream_index;
                QString label;
                bool is_selected;
            };
            std::vector<AudioTrackMenuEntry> audio_tracks;
            if (player_)
            {
                lnplayer_enumerate_audio_tracks(
                    player_,
                    [](const lnplayer_audio_track_info_t* track, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<AudioTrackMenuEntry>*>(user_data);
                        QString label = track->title_utf8 && track->title_utf8[0]
                            ? QString::fromUtf8(track->title_utf8)
                            : QString::fromUtf8(track->language_utf8 ? track->language_utf8 : "");
                        if (label.isEmpty())
                            label = QString::fromUtf8(track->codec_utf8 ? track->codec_utf8 : "Audio");
                        if (track->channels > 0)
                            label += QStringLiteral(" (%1 ch)").arg(track->channels);
                        if (track->is_default)
                            label.prepend(QStringLiteral("[默认] "));
                        entries->push_back({track->stream_index, label, track->is_selected != 0});
                    },
                    &audio_tracks);
            }
            for (const auto& track : audio_tracks)
            {
                QAction* action = audio_track_menu->addAction(track.label);
                action->setCheckable(true);
                action->setChecked(track.is_selected);
                action->setData(track.stream_index);
            }
            if (audio_tracks.empty())
            {
                QAction* unavailable = audio_track_menu->addAction(QStringLiteral("无可用音轨"));
                unavailable->setEnabled(false);
            }

            QMenu* subtitle_menu = menu.addMenu(QStringLiteral("字幕"));
            QAction* subtitles_enabled = subtitle_menu->addAction(QStringLiteral("启用字幕"));
            subtitles_enabled->setCheckable(true);
            subtitles_enabled->setChecked(subtitles_enabled_);
            QMenu* embedded_subtitle_menu = subtitle_menu->addMenu(QStringLiteral("内嵌字幕"));
            QAction* disable_embedded_subtitle =
                embedded_subtitle_menu->addAction(QStringLiteral("禁用内嵌字幕"));
            disable_embedded_subtitle->setCheckable(true);
            struct SubtitleTrackMenuEntry
            {
                int stream_index;
                QString label;
                bool is_selected;
            };
            std::vector<SubtitleTrackMenuEntry> embedded_subtitle_tracks;
            if (player_)
            {
                lnplayer_enumerate_subtitle_tracks(
                    player_,
                    [](const lnplayer_subtitle_track_info_t* track, void* user_data)
                    {
                        auto* entries = static_cast<std::vector<SubtitleTrackMenuEntry>*>(user_data);
                        QString label = track->title_utf8 && track->title_utf8[0]
                            ? QString::fromUtf8(track->title_utf8)
                            : QString::fromUtf8(track->language_utf8 ? track->language_utf8 : "");
                        if (label.isEmpty())
                            label = QString::fromUtf8(track->codec_utf8 ? track->codec_utf8 : "Subtitle");
                        if (track->is_default)
                            label.prepend(QStringLiteral("[默认] "));
                        entries->push_back({track->stream_index, label, track->is_selected != 0});
                    },
                    &embedded_subtitle_tracks);
            }
            bool has_selected_embedded_subtitle = false;
            for (const auto& track : embedded_subtitle_tracks)
            {
                QAction* action = embedded_subtitle_menu->addAction(track.label);
                action->setCheckable(true);
                action->setChecked(track.is_selected);
                action->setData(track.stream_index);
                has_selected_embedded_subtitle =
                    has_selected_embedded_subtitle || track.is_selected;
            }
            disable_embedded_subtitle->setChecked(!has_selected_embedded_subtitle);
            if (embedded_subtitle_tracks.empty())
            {
                QAction* unavailable =
                    embedded_subtitle_menu->addAction(QStringLiteral("无可用内嵌字幕"));
                unavailable->setEnabled(false);
            }
            QAction* subtitle_delay = subtitle_menu->addAction(QStringLiteral("字幕延迟 +500 ms"));
            QAction* subtitle_advance = subtitle_menu->addAction(QStringLiteral("字幕提前 500 ms"));
            QAction* subtitle_reset = subtitle_menu->addAction(QStringLiteral("重置字幕时间"));
            QAction* previous_frame = menu.addAction(QStringLiteral("上一帧"));
            QAction* next_frame = menu.addAction(QStringLiteral("下一帧"));
            QAction* set_loop_a = menu.addAction(QStringLiteral("设置 A-B 循环起点"));
            QAction* set_loop_b = menu.addAction(QStringLiteral("设置 A-B 循环终点"));
            QAction* clear_loop = menu.addAction(QStringLiteral("清除 A-B 循环"));
            QMenu* speed_menu = menu.addMenu(QStringLiteral("播放速度"));
            const float speeds[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
            for (float speed : speeds)
            {
                QAction* speed_action = speed_menu->addAction(
                    QStringLiteral("%1x").arg(speed, 0, 'f', 2));
                speed_action->setCheckable(true);
                speed_action->setChecked(std::abs(playback_rate_ - speed) < 0.001f);
                speed_action->setData(speed);
            }
            menu.addSeparator();
            QMenu* aspect_menu = menu.addMenu(QStringLiteral("画面比例"));
            QAction* fit = aspect_menu->addAction(QStringLiteral("原始比例"));
            QAction* ratio_16_9 = aspect_menu->addAction(QStringLiteral("16:9"));
            QAction* ratio_4_3 = aspect_menu->addAction(QStringLiteral("4:3"));
            QAction* stretch = aspect_menu->addAction(QStringLiteral("拉伸填充"));
            QAction* aspect_actions[] = {fit, ratio_16_9, ratio_4_3, stretch};
            for (QAction* action : aspect_actions)
                action->setCheckable(true);
            fit->setChecked(video_aspect_ratio_override_ == 0.0f && !video_stretch_);
            ratio_16_9->setChecked(
                std::abs(video_aspect_ratio_override_ - 16.0f / 9.0f) < 0.001f);
            ratio_4_3->setChecked(
                std::abs(video_aspect_ratio_override_ - 4.0f / 3.0f) < 0.001f);
            stretch->setChecked(video_stretch_);

            QMenu* filter_menu = menu.addMenu(QStringLiteral("视频滤镜"));
            QAction* brightness_down = filter_menu->addAction(QStringLiteral("亮度 -"));
            QAction* brightness_up = filter_menu->addAction(QStringLiteral("亮度 +"));
            QAction* contrast_down = filter_menu->addAction(QStringLiteral("对比度 -"));
            QAction* contrast_up = filter_menu->addAction(QStringLiteral("对比度 +"));
            QAction* saturation_down = filter_menu->addAction(QStringLiteral("饱和度 -"));
            QAction* saturation_up = filter_menu->addAction(QStringLiteral("饱和度 +"));
            QAction* hue_down = filter_menu->addAction(QStringLiteral("色调 -"));
            QAction* hue_up = filter_menu->addAction(QStringLiteral("色调 +"));
            QAction* sharpen_down = filter_menu->addAction(QStringLiteral("锐化 -"));
            QAction* sharpen_up = filter_menu->addAction(QStringLiteral("锐化 +"));
            QAction* denoise_down = filter_menu->addAction(QStringLiteral("降噪 -"));
            QAction* denoise_up = filter_menu->addAction(QStringLiteral("降噪 +"));
            QAction* grayscale = filter_menu->addAction(QStringLiteral("灰度"));
            grayscale->setCheckable(true);
            grayscale->setChecked(video_grayscale_);
            QAction* filter_reset = filter_menu->addAction(QStringLiteral("重置滤镜"));

            menu.addSeparator();
            QAction* settings_action = menu.addAction(QStringLiteral("播放器设置..."));

            QAction* selected = menu.exec(video_surface_->mapToGlobal(point));
            if (selected == open_media)
                openFile();
            else if (selected == open_folder)
            {
                const QString directory = QFileDialog::getExistingDirectory(
                    this, QStringLiteral("打开文件夹"));
                if (!directory.isEmpty())
                    openFiles({QUrl::fromLocalFile(directory)});
            }
            else if (selected == play_pause)
                togglePlayback();
            else if (selected == reset_zoom)
            {
                video_surface_->adjust_zoom(-100);
                showOsd(QStringLiteral("画面缩放 100%"));
            }
            else if (selected == stop)
                postCommand([this]
                {
                    if (player_)
                        lnplayer_stop(player_);
                });
            else if (selected == fullscreen)
            {
                if (isFullScreen())
                    showNormal();
                else
                    showFullScreen();
            }
            else if (selected == screenshot)
                takeScreenshot();
            else if (selected && selected->parent() == audio_track_menu)
                selectAudioTrack(selected->data().toInt());
            else if (selected == disable_embedded_subtitle ||
                (selected && selected->parent() == embedded_subtitle_menu))
            {
                selectSubtitleTrack(selected == disable_embedded_subtitle
                    ? -1
                    : selected->data().toInt());
            }
            else if (selected == subtitles_enabled)
            {
                subtitles_enabled_ = subtitles_enabled->isChecked();
                const bool enabled = subtitles_enabled_;
                postCommand([this, enabled]
                {
                    if (player_)
                        lnplayer_set_subtitles_enabled(player_, enabled ? 1 : 0);
                });
                pending_video_frame_valid_ = false;
                requestVideoFrame();
            }
            else if (selected == subtitle_delay)
                setSubtitleOffset(subtitle_offset_ms_ + 500);
            else if (selected == subtitle_advance)
                setSubtitleOffset(subtitle_offset_ms_ - 500);
            else if (selected == subtitle_reset)
                setSubtitleOffset(0);
            else if (selected == previous_frame)
                stepVideoFrame(true);
            else if (selected == next_frame)
                stepVideoFrame();
            else if (selected == set_loop_a)
            {
                loop_a_ms_ = current_position_ms_;
                if (loop_b_ms_ <= loop_a_ms_)
                    loop_b_ms_ = -1;
                showOsd(QStringLiteral("A 点：%1").arg(format_position(loop_a_ms_)));
            }
            else if (selected == set_loop_b)
            {
                if (loop_a_ms_ < 0 || current_position_ms_ <= loop_a_ms_)
                {
                    statusBar()->showMessage(QStringLiteral("请先设置有效的 A 点"));
                }
                else
                {
                    loop_b_ms_ = current_position_ms_;
                    showOsd(QStringLiteral("A-B 循环：%1 - %2")
                        .arg(format_position(loop_a_ms_), format_position(loop_b_ms_)));
                }
            }
            else if (selected == clear_loop)
            {
                loop_a_ms_ = -1;
                loop_b_ms_ = -1;
                showOsd(QStringLiteral("已清除 A-B 循环"));
            }
            else if (selected && selected->parent() == speed_menu)
                setPlaybackRate(selected->data().toFloat());
            else if (selected == fit || selected == ratio_16_9 ||
                selected == ratio_4_3 || selected == stretch)
            {
                video_stretch_ = selected == stretch;
                video_aspect_ratio_override_ = selected == ratio_16_9
                    ? 16.0f / 9.0f
                    : selected == ratio_4_3 ? 4.0f / 3.0f : 0.0f;
                video_surface_->set_aspect_ratio(video_aspect_ratio_override_);
                video_surface_->set_stretch(video_stretch_);
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                settings.setValue(
                    QStringLiteral("Video/Display/AspectRatio"),
                    video_aspect_ratio_override_);
                settings.setValue(QStringLiteral("Video/Display/Stretch"), video_stretch_);
                settings.sync();
                requestVideoFrame();
            }
            else if (selected == brightness_down || selected == brightness_up ||
                selected == contrast_down || selected == contrast_up ||
                selected == saturation_down || selected == saturation_up ||
                selected == hue_down || selected == hue_up ||
                selected == sharpen_down || selected == sharpen_up ||
                selected == denoise_down || selected == denoise_up ||
                selected == grayscale || selected == filter_reset)
            {
                if (selected == brightness_down)
                    video_brightness_ = std::clamp(video_brightness_ - 0.1f, -1.0f, 1.0f);
                else if (selected == brightness_up)
                    video_brightness_ = std::clamp(video_brightness_ + 0.1f, -1.0f, 1.0f);
                else if (selected == contrast_down)
                    video_contrast_ = std::clamp(video_contrast_ - 0.1f, 0.0f, 2.0f);
                else if (selected == contrast_up)
                    video_contrast_ = std::clamp(video_contrast_ + 0.1f, 0.0f, 2.0f);
                else if (selected == saturation_down)
                    video_saturation_ = std::clamp(video_saturation_ - 0.1f, 0.0f, 2.0f);
                else if (selected == saturation_up)
                    video_saturation_ = std::clamp(video_saturation_ + 0.1f, 0.0f, 2.0f);
                else if (selected == hue_down)
                    video_hue_degrees_ = std::clamp(video_hue_degrees_ - 5.0f, -180.0f, 180.0f);
                else if (selected == hue_up)
                    video_hue_degrees_ = std::clamp(video_hue_degrees_ + 5.0f, -180.0f, 180.0f);
                else if (selected == sharpen_down)
                    video_sharpness_ = std::clamp(video_sharpness_ - 0.1f, 0.0f, 2.0f);
                else if (selected == sharpen_up)
                    video_sharpness_ = std::clamp(video_sharpness_ + 0.1f, 0.0f, 2.0f);
                else if (selected == denoise_down)
                    video_denoise_ = std::clamp(video_denoise_ - 0.1f, 0.0f, 1.0f);
                else if (selected == denoise_up)
                    video_denoise_ = std::clamp(video_denoise_ + 0.1f, 0.0f, 1.0f);
                else if (selected == grayscale)
                    video_grayscale_ = grayscale->isChecked();
                else
                {
                    video_brightness_ = 0.0f;
                    video_contrast_ = 1.0f;
                    video_saturation_ = 1.0f;
                    video_hue_degrees_ = 0.0f;
                    video_sharpness_ = 0.0f;
                    video_denoise_ = 0.0f;
                    video_grayscale_ = false;
                }

                video_surface_->set_video_filter(
                    video_brightness_, video_contrast_, video_saturation_,
                    video_hue_degrees_, video_sharpness_, video_denoise_, video_grayscale_);
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                settings.setValue(QStringLiteral("Video/Filter/Brightness"), video_brightness_);
                settings.setValue(QStringLiteral("Video/Filter/Contrast"), video_contrast_);
                settings.setValue(QStringLiteral("Video/Filter/Saturation"), video_saturation_);
                settings.setValue(QStringLiteral("Video/Filter/HueDegrees"), video_hue_degrees_);
                settings.setValue(QStringLiteral("Video/Filter/Sharpness"), video_sharpness_);
                settings.setValue(QStringLiteral("Video/Filter/Denoise"), video_denoise_);
                settings.setValue(QStringLiteral("Video/Filter/Grayscale"), video_grayscale_);
                settings.sync();
                pending_video_frame_valid_ = false;
                requestVideoFrame();
                statusBar()->showMessage(QStringLiteral("视频滤镜已更新"));
            }
            else if (selected == settings_action)
                openVideoSettings();
        });
        layout->addWidget(video_surface_, 1);
        playlist_panel_->setGeometry(
            std::max(0, video_surface_->width() - playlist_panel_->width()),
            0, playlist_panel_->width(), video_surface_->height());
    }
    else
    {
        auto* audio_view = new QWidget(content);
        auto* audio_layout = new QVBoxLayout(audio_view);
        audio_layout->addStretch(1);
        media_title_label_->setAlignment(Qt::AlignCenter);
        QFont title_font = media_title_label_->font();
        title_font.setPointSize(16);
        media_title_label_->setFont(title_font);
        audio_layout->addWidget(media_title_label_);
        audio_layout->addStretch(1);
        library_search_->setPlaceholderText(QStringLiteral("搜索标题、艺术家或专辑"));
        auto* search_button = new QPushButton(QStringLiteral("搜索"), audio_view);
        auto* search_layout = new QHBoxLayout();
        search_layout->addWidget(library_search_, 1);
        search_layout->addWidget(search_button);
        audio_layout->addLayout(search_layout);
        audio_layout->addWidget(library_list_, 2);
        connect(search_button, &QPushButton::clicked, this, [this]
        {
            refreshLibrary();
        });
        connect(library_search_, &QLineEdit::returnPressed, this, [this]
        {
            refreshLibrary();
        });
        connect(library_list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item)
        {
            const QString path = item->data(Qt::UserRole).toString();
            openFiles({QUrl::fromLocalFile(path)});
        });
        layout->addWidget(audio_view, 1);
    }

    seek_slider_->setRange(0, 1000);
    seek_slider_->setMouseTracking(true);
    seek_slider_->installEventFilter(this);
    seek_slider_->setEnabled(false);
    volume_slider_->setRange(0, 1000);
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        const int saved_volume = std::clamp(
            settings.value(QStringLiteral("Playback/Volume"), 1000).toInt(), 0, 1000);
        muted_volume_ = std::clamp(
            settings.value(QStringLiteral("Playback/MutedVolume"), 1000).toInt(), 1, 1000);
        volume_slider_->setValue(saved_volume);
        if (saved_volume > 0)
            muted_volume_ = saved_volume;
    }
    volume_slider_->setMaximumWidth(160);

    control_panel_ = new QWidget(
        mode_ == PlayerMode::Video ? video_surface_ : content);
    control_panel_->setAutoFillBackground(false);
    control_panel_->setObjectName(QStringLiteral("controlPanel"));
    control_panel_->setMouseTracking(true);
    auto* control_panel_layout = new QVBoxLayout(control_panel_);
    control_panel_layout->setContentsMargins(12, 5, 12, 5);
    control_panel_layout->setSpacing(2);
    auto* seek_layout = new QHBoxLayout();
    seek_layout->setContentsMargins(0, 0, 0, 0);
    seek_layout->setSpacing(6);
    position_label_->setMinimumWidth(86);
    position_label_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    seek_layout->addWidget(position_label_);
    seek_layout->addWidget(seek_slider_, 1);

    auto* volume_button = new QPushButton(control_panel_);
    volume_button->setProperty("iconOnly", true);
    volume_button->setIcon(PlayerIcons::make(volume_slider_->value() == 0
        ? PlayerIcons::Name::VolumeMuted : PlayerIcons::Name::Volume));
    volume_button->setIconSize(QSize(20, 20));
    volume_button->setFixedSize(34, 30);
    volume_button->setToolTip(QStringLiteral("静音切换"));
    volume_button->setAccessibleName(QStringLiteral("静音切换"));
    volume_button->setStyleSheet(QStringLiteral("QPushButton { padding: 1px; border: 0; background: transparent; }"));
    seek_layout->addWidget(volume_button);
    volume_slider_->setMaximumWidth(104);
    volume_slider_->setFixedWidth(104);
    seek_layout->addWidget(volume_slider_);
    connect(volume_slider_, &QSlider::valueChanged, volume_button, [volume_button](int value)
    {
        volume_button->setIcon(PlayerIcons::make(value == 0
            ? PlayerIcons::Name::VolumeMuted : PlayerIcons::Name::Volume));
    });
    connect(volume_button, &QPushButton::clicked, this, [this]
    {
        if (volume_slider_->value() == 0)
            volume_slider_->setValue(muted_volume_);
        else
        {
            muted_volume_ = volume_slider_->value();
            volume_slider_->setValue(0);
        }
    });
    control_panel_layout->addLayout(seek_layout);

    auto* control_layout = new QHBoxLayout();
    control_layout->setContentsMargins(0, 0, 0, 0);
    control_layout->setSpacing(3);
    play_button_->setObjectName(QStringLiteral("playButton"));
    const auto set_transport_icon = [this](QPushButton* button, PlayerIcons::Name icon,
        const QString& tooltip, const QString& accessible_name)
    {
        button->setText(QString());
        button->setIcon(PlayerIcons::make(icon));
        button->setIconSize(QSize(20, 20));
        button->setFixedSize(36, 32);
        button->setToolTip(tooltip);
        button->setAccessibleName(accessible_name);
        button->setStyleSheet(QString());
    };
    set_transport_icon(play_button_, PlayerIcons::Name::Play,
        QStringLiteral("播放"), QStringLiteral("播放"));
    set_transport_icon(stop_button_, PlayerIcons::Name::Stop,
        QStringLiteral("停止"), QStringLiteral("停止"));
    auto* previous_button = new QPushButton(control_panel_);
    auto* next_button = new QPushButton(control_panel_);
    set_transport_icon(previous_button, PlayerIcons::Name::Previous,
        QStringLiteral("上一项"), QStringLiteral("上一项"));
    set_transport_icon(next_button, PlayerIcons::Name::Next,
        QStringLiteral("下一项"), QStringLiteral("下一项"));

    control_layout->addWidget(play_button_);
    control_layout->addWidget(previous_button);
    control_layout->addWidget(next_button);
    control_layout->addWidget(stop_button_);
    control_layout->addSpacing(6);
    const auto find_menu = [this](const QString& title) -> QMenu*
    {
        if (!menu_bar_)
            return nullptr;
        for (QAction* action : menu_bar_->actions())
            if (action->text() == title)
                return action->menu();
        return nullptr;
    };
    if (mode_ == PlayerMode::Video)
    {
        playlist_toggle_button_ = new QPushButton(control_panel_);
    playlist_toggle_button_->setProperty("iconOnly", true);
        playlist_toggle_button_->setCheckable(true);
        playlist_toggle_button_->setChecked(playlist_panel_visible_);
        playlist_toggle_button_->setIcon(PlayerIcons::make(PlayerIcons::Name::Playlist,
            playlist_panel_visible_ ? QColor(QStringLiteral("#a9d5ff"))
                : QColor(QStringLiteral("#dce5f0"))));
        playlist_toggle_button_->setIconSize(QSize(20, 20));
        playlist_toggle_button_->setFixedSize(36, 32);
        playlist_toggle_button_->setToolTip(playlist_panel_visible_
            ? QStringLiteral("隐藏播放列表") : QStringLiteral("显示播放列表"));
        playlist_toggle_button_->setAccessibleName(playlist_toggle_button_->toolTip());
        playlist_toggle_button_->setStyleSheet(QString());
        connect(playlist_toggle_button_, &QPushButton::clicked, this, [this](bool visible)
        {
            if (playlist_visibility_action_)
                playlist_visibility_action_->setChecked(visible);
        });
        connect(playlist_visibility_action_, &QAction::toggled, this, [this](bool visible)
        {
            if (!playlist_toggle_button_)
                return;
            playlist_toggle_button_->setChecked(visible);
            playlist_toggle_button_->setIcon(PlayerIcons::make(PlayerIcons::Name::Playlist,
                visible ? QColor(QStringLiteral("#a9d5ff"))
                    : QColor(QStringLiteral("#dce5f0"))));
            playlist_toggle_button_->setToolTip(visible
                ? QStringLiteral("隐藏播放列表") : QStringLiteral("显示播放列表"));
            playlist_toggle_button_->setAccessibleName(playlist_toggle_button_->toolTip());
        });
    }
    auto* audio_tracks_button = new QPushButton(control_panel_);
    audio_tracks_button->setProperty("iconOnly", true);
    audio_tracks_button->setIcon(PlayerIcons::make(PlayerIcons::Name::AudioTrack));
    audio_tracks_button->setIconSize(QSize(20, 20));
    audio_tracks_button->setFixedSize(36, 32);
    audio_tracks_button->setToolTip(QStringLiteral("切换音轨"));
    audio_tracks_button->setAccessibleName(QStringLiteral("切换音轨"));
    audio_tracks_button->setStyleSheet(QString());
    if (QMenu* audio_tracks_menu = find_menu(QStringLiteral("音轨")))
        audio_tracks_button->setMenu(audio_tracks_menu);
    auto* subtitles_button = new QPushButton(control_panel_);
    subtitles_button->setProperty("iconOnly", true);
    subtitles_button->setIcon(PlayerIcons::make(PlayerIcons::Name::Subtitles));
    subtitles_button->setIconSize(QSize(20, 20));
    subtitles_button->setFixedSize(36, 32);
    subtitles_button->setToolTip(QStringLiteral("切换字幕"));
    subtitles_button->setAccessibleName(QStringLiteral("切换字幕"));
    subtitles_button->setStyleSheet(QString());
    if (QMenu* subtitles_menu = find_menu(QStringLiteral("字幕")))
        subtitles_button->setMenu(subtitles_menu);
    control_layout->addStretch(1);
    if (playlist_toggle_button_)
        control_layout->addWidget(playlist_toggle_button_);
    control_layout->addWidget(audio_tracks_button);
    control_layout->addWidget(subtitles_button);
    connect(previous_button, &QPushButton::clicked, this, [this] { playPrevious(); });
    connect(next_button, &QPushButton::clicked, this, [this] { playNext(); });
    control_panel_layout->addLayout(control_layout);
    status_label_->setMinimumHeight(16);
    status_label_->setContentsMargins(2, 0, 2, 0);
    control_panel_layout->addWidget(status_label_);
    setCentralWidget(content);
    if (mode_ == PlayerMode::Video)
    {
        control_panel_->setParent(video_surface_);
        control_panel_->setAttribute(Qt::WA_NativeWindow);
        control_panel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        control_panel_->setFixedHeight(control_panel_->sizeHint().height());
        updateVideoOverlayGeometry();
        control_panel_->show();
    }
    if (menu_bar_)
    {
        menu_bar_->setParent(content);
        menu_bar_->hide();
    }
    if (title_bar_)
    {
        if (QWidget* previous_parent = title_bar_->parentWidget())
        {
            if (QLayout* previous_layout = previous_parent->layout())
                previous_layout->removeWidget(title_bar_);
        }
        title_bar_->setParent(video_surface_);
        title_bar_->setAttribute(Qt::WA_NativeWindow);
        title_bar_->setGeometry(6, 6,
            std::max(0, video_surface_->width() - 12), title_bar_->sizeHint().height());
        title_bar_->hide();
        title_bar_->raise();
    }

    fullscreen_hide_timer_->setSingleShot(true);
    fullscreen_hide_timer_->setInterval(fullscreen_hide_delay_ms_);
    connect(fullscreen_hide_timer_, &QTimer::timeout, this, [this]
    {
        if (mode_ != PlayerMode::Video)
            return;
        if (title_bar_)
            title_bar_->hide();
        if (playlist_panel_)
            playlist_panel_->hide();
        if (control_panel_)
            control_panel_->hide();
        if (isFullScreen())
            setCursor(Qt::BlankCursor);
    });
    if (video_surface_)
    {
        video_surface_->setMouseTracking(true);
        video_surface_->set_gesture_enabled(mouse_gestures_enabled_);
        video_surface_->installEventFilter(this);
    }
    control_panel_->setMouseTracking(true);
    control_panel_->installEventFilter(this);
    const auto control_children = control_panel_->findChildren<QWidget*>();
    for (QWidget* child : control_children)
    {
        child->setMouseTracking(true);
        child->installEventFilter(this);
    }
    if (playlist_panel_)
    {
        playlist_panel_->setMouseTracking(true);
        playlist_panel_->installEventFilter(this);
        const auto playlist_children = playlist_panel_->findChildren<QWidget*>();
        for (QWidget* child : playlist_children)
        {
            child->setMouseTracking(true);
            child->installEventFilter(this);
        }
    }

    if (mode_ == PlayerMode::Video)
    {
        auto shortcut_focus_blocked = [](QWidget* focus)
        {
            return QApplication::activePopupWidget() || QApplication::activeModalWidget() ||
                qobject_cast<QLineEdit*>(focus) ||
                qobject_cast<QPlainTextEdit*>(focus) ||
                qobject_cast<QTextEdit*>(focus) ||
                qobject_cast<QAbstractItemView*>(focus) ||
                qobject_cast<QAbstractButton*>(focus) ||
                qobject_cast<QAbstractSlider*>(focus) ||
                qobject_cast<QAbstractSpinBox*>(focus) ||
                qobject_cast<QComboBox*>(focus) ||
                qobject_cast<QKeySequenceEdit*>(focus);
        };
        auto bind_shortcut = [this, shortcut_focus_blocked](const QString& id, const QString& label,
            int key, std::function<void()> action)
        {
            const QKeySequence default_sequence(key);
            QSettings settings(settings_file_path(), QSettings::IniFormat);
            const QString setting_key = QStringLiteral("Video/Shortcuts/") + id;
            const QKeySequence sequence = QKeySequence::fromString(
                settings.value(setting_key, default_sequence.toString()).toString());
            auto* shortcut = new QShortcut(sequence, this);
            shortcut->setContext(Qt::WindowShortcut);
            shortcut->setProperty("commandLabel", label);
            shortcut->setProperty("defaultSequence", default_sequence.toString());
            shortcuts_.insert(id, shortcut);
            connect(shortcut, &QShortcut::activated, this,
                [action = std::move(action), shortcut_focus_blocked]
            {
                if (shortcut_focus_blocked(QApplication::focusWidget()))
                    return;
                action();
            });
        };
        bind_shortcut(QStringLiteral("play_pause"), QStringLiteral("播放/暂停"), Qt::Key_Space, [this] { togglePlayback(); });
        bind_shortcut(QStringLiteral("fullscreen"), QStringLiteral("全屏切换"), Qt::Key_F11, [this]
        {
            if (isFullScreen())
                showNormal();
            else
                showFullScreen();
        });
        bind_shortcut(QStringLiteral("exit_fullscreen"), QStringLiteral("退出全屏"), Qt::Key_Escape, [this]
        {
            if (isFullScreen())
                showNormal();
        });
        bind_shortcut(QStringLiteral("seek_backward"), QStringLiteral("后退 5 秒"), Qt::Key_Left, [this] { seekRelative(-5000); });
        bind_shortcut(QStringLiteral("seek_forward"), QStringLiteral("前进 5 秒"), Qt::Key_Right, [this] { seekRelative(5000); });
        bind_shortcut(QStringLiteral("frame_backward"), QStringLiteral("上一帧"), Qt::Key_Comma, [this] { stepVideoFrame(true); });
        bind_shortcut(QStringLiteral("frame_forward"), QStringLiteral("下一帧"), Qt::Key_Period, [this] { stepVideoFrame(); });
        bind_shortcut(QStringLiteral("volume_up"), QStringLiteral("音量增加"), Qt::Key_Up, [this]
        {
            volume_slider_->setValue(std::min(1000, volume_slider_->value() + 50));
        });
        bind_shortcut(QStringLiteral("volume_down"), QStringLiteral("音量降低"), Qt::Key_Down, [this]
        {
            volume_slider_->setValue(std::max(0, volume_slider_->value() - 50));
        });
        bind_shortcut(QStringLiteral("previous_item"), QStringLiteral("上一项"), Qt::Key_PageUp, [this] { playPrevious(); });
        bind_shortcut(QStringLiteral("next_item"), QStringLiteral("下一项"), Qt::Key_PageDown, [this] { playNext(); });
        bind_shortcut(QStringLiteral("seek_backward_large"), QStringLiteral("后退 30 秒"), Qt::CTRL | Qt::Key_Left, [this] { seekRelative(-30000); });
        bind_shortcut(QStringLiteral("seek_forward_large"), QStringLiteral("前进 30 秒"), Qt::CTRL | Qt::Key_Right, [this] { seekRelative(30000); });
        bind_shortcut(QStringLiteral("open_file"), QStringLiteral("打开文件"), Qt::Key_F3, [this] { openFile(); });
        bind_shortcut(QStringLiteral("open_settings"), QStringLiteral("打开设置"), Qt::Key_F5, [this] { openVideoSettings(); });
        bind_shortcut(QStringLiteral("playback_info"), QStringLiteral("播放信息"), Qt::Key_F7, [this] { showPlaybackInfo(); });
        bind_shortcut(QStringLiteral("toggle_playlist"), QStringLiteral("显示/隐藏播放列表"), Qt::Key_F6, [this]
        {
            if (playlist_visibility_action_)
                playlist_visibility_action_->setChecked(!playlist_visibility_action_->isChecked());
        });
        bind_shortcut(QStringLiteral("toggle_topmost"), QStringLiteral("窗口置顶"), Qt::CTRL | Qt::Key_T, [this]
        {
            if (always_on_top_action_)
                always_on_top_action_->setChecked(!always_on_top_action_->isChecked());
        });
        bind_shortcut(QStringLiteral("speed_down"), QStringLiteral("降低播放速度"), Qt::Key_X, [this]
        {
            setPlaybackRate(std::max(0.25f, playback_rate_ - 0.25f));
        });
        bind_shortcut(QStringLiteral("speed_up"), QStringLiteral("提高播放速度"), Qt::Key_C, [this]
        {
            setPlaybackRate(std::min(4.0f, playback_rate_ + 0.25f));
        });
        bind_shortcut(QStringLiteral("speed_reset"), QStringLiteral("恢复正常速度"), Qt::Key_Z, [this] { setPlaybackRate(1.0f); });
        bind_shortcut(QStringLiteral("screenshot"), QStringLiteral("截图"), Qt::Key_K, [this] { takeScreenshot(); });
        bind_shortcut(QStringLiteral("mute"), QStringLiteral("静音切换"), Qt::Key_M, [this]
        {
            if (volume_slider_->value() == 0)
                volume_slider_->setValue(muted_volume_);
            else
            {
                muted_volume_ = volume_slider_->value();
                volume_slider_->setValue(0);
            }
        });

        const auto update_shortcut_availability =
            [this, shortcut_focus_blocked](QWidget*, QWidget* focus)
        {
            const bool enabled = !shortcut_focus_blocked(focus);
            for (QShortcut* shortcut : shortcuts_)
                shortcut->setEnabled(enabled);
        };
        connect(qApp, &QApplication::focusChanged, this, update_shortcut_availability);
        update_shortcut_availability(nullptr, QApplication::focusWidget());
    }
    statusBar()->showMessage(QStringLiteral("正在初始化播放器"));
    if (mode_ == PlayerMode::Video)
    {
        updateVideoOverlayGeometry();
        if (fullscreen_hide_delay_ms_ > 0)
            fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
    }

    const uint32_t initial_volume = static_cast<uint32_t>(volume_slider_->value());
    const float initial_playback_rate = playback_rate_;
    const int64_t initial_subtitle_offset_ms = subtitle_offset_ms_;
    const bool initial_subtitles_enabled = subtitles_enabled_;
    void* initial_video_device = mode_ == PlayerMode::Video && video_surface_
        ? video_surface_->native_device()
        : nullptr;
    video_device_identity_ = initial_video_device;
    postCommand([this, initial_volume, initial_playback_rate, initial_subtitle_offset_ms, initial_subtitles_enabled, initial_video_device]
    {
        lnplayer_config_t config{};
        config.struct_size = sizeof(config);
        config.event_callback = +[](
            lnplayer_t*,
            const lnplayer_event_t* event,
            void* user_data)
        {
            auto* window = static_cast<MainWindow*>(user_data);
            const lnplayer_event_type_t type = event->type;
            const lnplayer_result_t result = event->result;
            const lnplayer_state_t state = event->state;
            const int64_t position_ms = event->position_ms;
            const int64_t duration_ms = event->duration_ms;
            const uint64_t generation = event->generation;
            const QString message = event->message
                ? QString::fromUtf8(event->message)
                : QString();

            QMetaObject::invokeMethod(window, [=]
            {
                window->handleCoreEvent(
                    type,
                    result,
                    state,
                    position_ms,
                    duration_ms,
                    generation,
                    message);
            }, Qt::QueuedConnection);
        };
        config.user_data = this;

        lnplayer_t* created_player = nullptr;
        const lnplayer_result_t result = lnplayer_create(&config, &created_player);
        if (result == LNPLAYER_RESULT_OK)
        {
            player_.store(created_player, std::memory_order_release);
            lnplayer_set_volume(created_player, initial_volume);
            if (mode_ == PlayerMode::Video)
            {
                lnplayer_set_playback_rate(created_player, initial_playback_rate);
                lnplayer_set_subtitle_offset(created_player, initial_subtitle_offset_ms);
                lnplayer_set_subtitles_enabled(created_player, initial_subtitles_enabled ? 1 : 0);
            }
        }

        if (initial_video_device)
        {
            if (result == LNPLAYER_RESULT_OK)
            {
                const lnplayer_result_t device_result =
                    lnplayer_windows_set_d3d11_device(created_player, initial_video_device);
                if (device_result != LNPLAYER_RESULT_OK)
                {
                    static_cast<ID3D11Device*>(initial_video_device)->Release();
                    QMetaObject::invokeMethod(this, [this]
                    {
                        video_device_identity_ = nullptr;
                    }, Qt::QueuedConnection);
                }
            }
            else
            {
                static_cast<ID3D11Device*>(initial_video_device)->Release();
            }
        }
        QMetaObject::invokeMethod(this, [this, result]
        {
            if (result == LNPLAYER_RESULT_OK)
            {
                status_label_->setText(QStringLiteral("播放器已就绪"));
                statusBar()->showMessage(QStringLiteral("就绪"));
                snapshot_timer_->start();
                refreshSnapshot();
                if (mode_ == PlayerMode::Audio)
                    refreshLibrary();
            }
            else
            {
                status_label_->setText(QStringLiteral("播放器初始化失败"));
                statusBar()->showMessage(QStringLiteral("Core 初始化失败"));
            }
        }, Qt::QueuedConnection);
    });

    connect(play_button_, &QPushButton::clicked, this, [this]
    {
        togglePlayback();
    });

    connect(stop_button_, &QPushButton::clicked, this, [this]
    {
        postCommand([this]
        {
            if (player_)
                lnplayer_stop(player_);
        });
    });

    volume_settings_timer_->setSingleShot(true);
    volume_settings_timer_->setInterval(300);
    connect(volume_settings_timer_, &QTimer::timeout, this, [this]
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Playback/Volume"), volume_slider_->value());
        settings.setValue(QStringLiteral("Playback/MutedVolume"), muted_volume_);
        settings.sync();
    });

    connect(volume_slider_, &QSlider::valueChanged, this, [this](int value)
    {
        if (value > 0)
            muted_volume_ = value;
        volume_settings_timer_->start();

        showOsd(value == 0 ? QStringLiteral("静音")
            : QStringLiteral("音量 %1%").arg(value / 10));
        postCommand([this, value]
        {
            if (player_)
                lnplayer_set_volume(player_, static_cast<uint32_t>(value));
        });
    });

    connect(seek_slider_, &QSlider::sliderPressed, this, [this]
    {
        slider_dragging_ = true;
    });
    connect(seek_slider_, &QSlider::sliderReleased, this, [this]
    {
        slider_dragging_ = false;
        if (video_seek_pending_)
            return;
        viewing_historical_frame_ = false;
        video_frame_history_.clear();
        video_frame_history_index_ = -1;
        pending_video_frame_valid_ = false;
        video_end_of_stream_ = false;
        if (duration_ms_ <= 0)
        {
            video_seek_pending_ = false;
            return;
        }

        video_seek_pending_ = true;
        seek_slider_->setEnabled(false);
        video_timer_->start(0);
        const int64_t target = duration_ms_ * seek_slider_->value() / 1000;
        postCommand([this, target]
        {
            uint64_t operation_id = 0;
            const lnplayer_result_t result = player_
                ? lnplayer_seek(player_, target, &operation_id)
                : LNPLAYER_RESULT_INVALID_HANDLE;
            if (result != LNPLAYER_RESULT_OK)
            {
                QMetaObject::invokeMethod(this, [this]
                {
                    video_seek_pending_ = false;
                    seek_slider_->setEnabled(duration_ms_ > 0);
                    requestVideoFrame();
                }, Qt::QueuedConnection);
            }
        });
    });

    snapshot_timer_->setInterval(250);
    connect(snapshot_timer_, &QTimer::timeout, this, &MainWindow::refreshSnapshot);

    video_timer_->setSingleShot(true);
    video_timer_->setTimerType(Qt::PreciseTimer);
    connect(video_timer_, &QTimer::timeout, this, [this]
    {
        presentPendingVideoFrame();
        if (is_playing_ && has_video_ && !video_seek_pending_ &&
            !video_frame_pending_ && !pending_video_frame_valid_)
            requestVideoFrame();
    });

    device_timer_->setInterval(250);
    connect(device_timer_, &QTimer::timeout, this, [this]
    {
        if (!video_surface_)
            return;

        void* current_device = video_surface_->native_device();
        if (current_device != video_device_identity_)
        {
            video_device_identity_ = current_device;
            postCommand([this, current_device]
            {
                if (player_)
                {
                    const lnplayer_result_t result =
                        lnplayer_windows_set_d3d11_device(player_, current_device);
                    if (result != LNPLAYER_RESULT_OK && current_device)
                        static_cast<ID3D11Device*>(current_device)->Release();
                }
                else if (current_device)
                {
                    static_cast<ID3D11Device*>(current_device)->Release();
                }
            });
        }
        else if (current_device)
        {
            static_cast<ID3D11Device*>(current_device)->Release();
        }
    });
    if (mode_ == PlayerMode::Video)
        device_timer_->start();

    if (!initial_media_files.isEmpty())
    {
        QList<QUrl> initial_urls;
        initial_urls.reserve(initial_media_files.size());
        for (const QString& path : initial_media_files)
            initial_urls.append(QUrl::fromLocalFile(path));
        openFiles(initial_urls);
    }
    else if (mode_ == PlayerMode::Video)
        loadVideoPlaylist();
}

MainWindow::~MainWindow()
{
    qInfo() << "[Shutdown] MainWindow destructor entered";
    qInfo() << "[Shutdown] Removing child event filters";
    const auto descendants = findChildren<QObject*>();
    for (QObject* object : descendants)
        object->removeEventFilter(this);
    qInfo() << "[Shutdown] Child event filters removed";

    const auto widgets = findChildren<QWidget*>();
    qInfo() << "[Shutdown] Tracking widget destruction order:" << widgets.size();
    for (QWidget* widget : widgets)
    {
        const QString class_name = QString::fromLatin1(widget->metaObject()->className());
        const QString object_name = widget->objectName();
        QWidget* parent_widget = widget->parentWidget();
        const QString parent_class = parent_widget
            ? QString::fromLatin1(parent_widget->metaObject()->className())
            : QStringLiteral("<none>");
        const QString parent_name = parent_widget
            ? parent_widget->objectName()
            : QString();
        int depth = 0;
        for (QWidget* ancestor = parent_widget; ancestor; ancestor = ancestor->parentWidget())
            ++depth;

        QObject::connect(widget, &QObject::destroyed, this,
            [class_name, object_name, parent_class, parent_name, depth]
        {
            qInfo().noquote() << QStringLiteral(
                "[Shutdown] QWidget destroyed depth=%1 type=%2 name=%3 parent=%4/%5")
                .arg(depth)
                .arg(class_name, object_name, parent_class, parent_name);
        });
    }

    volume_settings_timer_->stop();
    fullscreen_hide_timer_->stop();
    osd_timer_->stop();
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Playback/Volume"), volume_slider_->value());
        settings.setValue(QStringLiteral("Playback/MutedVolume"), muted_volume_);
        settings.sync();
    }
    savePlaybackPosition();
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("Video/Window/Geometry"),
            normal_geometry_.isEmpty() ? saveGeometry() : normal_geometry_);
        settings.setValue(QStringLiteral("Video/Window/AlwaysOnTop"), always_on_top_);
        settings.sync();
    }
    saveVideoPlaylist();
    snapshot_timer_->stop();
    video_timer_->stop();
    device_timer_->stop();

    qInfo() << "[Shutdown] Waiting for UI tasks";
    task_tracker_->close_and_wait();
    qInfo() << "[Shutdown] UI tasks drained";

    if (lnplayer_t* player = player_.exchange(nullptr, std::memory_order_acq_rel))
    {
        qInfo() << "[Shutdown] Destroying Core player";
        const lnplayer_result_t result = lnplayer_destroy(player);
        qInfo() << "[Shutdown] Core player destroyed with result:" << result;
    }
    qInfo() << "[Shutdown] MainWindow destructor leaving";
}









































void MainWindow::refreshLibrary()
{
    lnplayer_t* player_handle = player_.load(std::memory_order_acquire);
    if (!player_handle)
        return;

    const QByteArray query_utf8 = library_search_->text().toUtf8();
    library_list_->clear();

    postLibrary([this, player_handle, query_utf8]
    {
        struct TrackItem
        {
            QString label;
            QString path;
        };
        struct SearchContext
        {
            std::vector<TrackItem> tracks;
        } context;

        const lnplayer_result_t result = lnplayer_library_search_tracks(
            player_handle,
            query_utf8.constData(),
            500,
            +[](const lnplayer_library_track_t* track, void* user_data)
            {
                auto* context = static_cast<SearchContext*>(user_data);
                const QString title = QString::fromUtf8(track->title_utf8);
                const QString artist = QString::fromUtf8(track->artist_utf8);
                context->tracks.push_back({
                    artist.isEmpty() ? title : QStringLiteral("%1 — %2").arg(title, artist),
                    QString::fromUtf8(track->path_utf8)
                });
            },
            &context);

        QMetaObject::invokeMethod(this,
            [this, result, tracks = std::move(context.tracks)]
        {
            for (const TrackItem& track : tracks)
            {
                auto* item = new QListWidgetItem(track.label, library_list_);
                item->setData(Qt::UserRole, track.path);
            }
            if (result != LNPLAYER_RESULT_OK)
                status_label_->setText(QStringLiteral("音乐库查询失败"));
        }, Qt::QueuedConnection);
    });
}

void MainWindow::showPlaybackInfo()
{
    if (mode_ != PlayerMode::Video)
        return;

    const QString media_path = playlist_index_ >= 0 && playlist_index_ < playlist_.size()
        ? playlist_[playlist_index_].toLocalFile()
        : QString();
    const QString duration = duration_ms_ >= 0
        ? format_position(duration_ms_)
        : QStringLiteral("--:--");
    const QString position = format_position(current_position_ms_);
    const QString video_state = has_video_
        ? QStringLiteral("有视频流")
        : QStringLiteral("无视频流");
    const QString audio_state = has_audio_
        ? QStringLiteral("有音轨")
        : QStringLiteral("无音轨");
    const QFileInfo media_file_info(media_path);
    const QString file_size = media_file_info.exists()
        ? QLocale().formattedDataSize(media_file_info.size())
        : QStringLiteral("未知");
    const QString resolution = video_width_ > 0 && video_height_ > 0
        ? QStringLiteral("%1 × %2").arg(video_width_).arg(video_height_)
        : QStringLiteral("未知");
    const QString renderer_state = video_surface_ && video_surface_->has_renderer()
        ? QStringLiteral("D3D11 渲染器已初始化")
        : QStringLiteral("D3D11 渲染器不可用");
    const double frame_rate = video_info_.frame_rate_den > 0
        ? static_cast<double>(video_info_.frame_rate_num) / video_info_.frame_rate_den
        : 0.0;
    const QString metadata = QStringLiteral(
        "容器：%1\n编码：%2（%3）\n解码器：%4\n帧率：%5\n码率：%6\n"
        "像素格式：%7 / %8-bit\n硬件解码：%9\nSAR：%10:%11\n旋转：%12°\n"
        "HDR：Mastering=%17/%18 nit MaxCLL=%19 MaxFALL=%20\n"
        "色彩：range=%13 primaries=%14 transfer=%15 matrix=%16")
        .arg(QString::fromUtf8(video_info_.container_long_name))
        .arg(QString::fromUtf8(video_info_.codec_name))
        .arg(QString::fromUtf8(video_info_.profile))
        .arg(QString::fromUtf8(video_info_.decoder_name))
        .arg(frame_rate > 0.0
            ? QString::number(frame_rate, 'f', 3) + QStringLiteral(" fps")
            : QStringLiteral("未知"))
        .arg(video_info_.bit_rate > 0
            ? QString::number(video_info_.bit_rate / 1000) + QStringLiteral(" kbps")
            : QStringLiteral("未知"))
        .arg(QString::fromUtf8(video_info_.pixel_format))
        .arg(video_info_.bit_depth)
        .arg(video_info_.hardware_decoding
            ? QStringLiteral("D3D11VA") : QStringLiteral("软件/未启用"))
        .arg(video_info_.sample_aspect_ratio_num)
        .arg(video_info_.sample_aspect_ratio_den)
        .arg(video_info_.rotation_degrees)
        .arg(video_info_.color_range)
        .arg(video_info_.color_primaries)
        .arg(video_info_.color_transfer)
        .arg(video_info_.color_space)
        .arg(video_info_.mastering_max_luminance, 0, 'f', 2)
        .arg(video_info_.mastering_min_luminance, 0, 'f', 4)
        .arg(video_info_.max_content_light_level)
        .arg(video_info_.max_frame_average_light_level);

    const QString details = QStringLiteral(
        "媒体：%1\n路径：%2\n大小：%14\n状态：%3\n位置：%4 / %5\n"
        "视频：%6\n分辨率：%7\n音轨：%8\n播放速度：%9x\n音量：%10%\n"
        "字幕偏移：%11 ms\n渲染：%12\n\n%13")
        .arg(media_title_label_->text())
        .arg(media_path.isEmpty() ? QStringLiteral("无") : media_path)
        .arg(status_label_->text())
        .arg(position)
        .arg(duration)
        .arg(video_state)
        .arg(resolution)
        .arg(audio_state)
        .arg(playback_rate_, 0, 'f', 2)
        .arg(volume_slider_->value() / 10)
        .arg(subtitle_offset_ms_)
        .arg(renderer_state)
        .arg(metadata)
        .arg(file_size);
    const bool fullscreen_before_dialog = isFullScreen();
    if (fullscreen_before_dialog)
        fullscreen_hide_timer_->stop();
    QMessageBox::information(this, QStringLiteral("播放信息"), details);
    if (fullscreen_before_dialog && isFullScreen() && fullscreen_hide_delay_ms_ > 0)
        fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
}

void MainWindow::positionOsd()
{
    if (!osd_label_ || !video_surface_)
        return;

    osd_label_->adjustSize();
    const int x = (video_surface_->width() - osd_label_->width()) / 2;
    const int y = osd_position_ == 0
        ? video_surface_->height() / 6
        : osd_position_ == 2
            ? video_surface_->height() * 5 / 6 - osd_label_->height()
            : (video_surface_->height() - osd_label_->height()) / 2;
    osd_label_->move(x, std::clamp(y, 0, std::max(0, video_surface_->height() - osd_label_->height())));
}

void MainWindow::showOsd(const QString& message)
{
    if (!osd_label_ || !video_surface_ || message.isEmpty())
        return;

    osd_label_->setText(message);
    positionOsd();
    osd_label_->raise();
    osd_label_->show();
    osd_timer_->start(osd_duration_ms_);
}

void MainWindow::fitWindowToVideoAspect(
    float aspect_ratio,
    const QSize& source_size)
{
    if (!video_surface_ || aspect_ratio <= 0.0f ||
        source_size.width() <= 0 || source_size.height() <= 0 ||
        isMaximized() || isFullScreen() || video_surface_->width() <= 0 ||
        video_surface_->height() <= 0)
        return;

    QScreen* target_screen = screen();
    if (!target_screen)
        return;

    // Start at the decoded frame's native pixel dimensions. Account for
    // rotation metadata when the display aspect is portrait/landscape while
    // the coded frame has the opposite orientation.
    int target_surface_width = source_size.width();
    int target_surface_height = source_size.height();
    const bool source_is_landscape = target_surface_width > target_surface_height;
    const bool display_is_landscape = aspect_ratio >= 1.0f;
    if (source_is_landscape != display_is_landscape)
        std::swap(target_surface_width, target_surface_height);

    // Preserve the display aspect ratio (including sample aspect ratio) while
    // keeping one dimension at native resolution whenever possible.
    if (target_surface_width / static_cast<float>(target_surface_height) > aspect_ratio)
        target_surface_width = qRound(target_surface_height * aspect_ratio);
    else
        target_surface_height = qRound(target_surface_width / aspect_ratio);

    const int window_chrome_width = width() - video_surface_->width();
    const int window_chrome_height = height() - video_surface_->height();
    const QRect available = target_screen->availableGeometry();
    const int maximum_window_width = std::max(1, available.width() - 24);
    const int maximum_window_height = std::max(1, available.height() - 48);

    int target_window_width = target_surface_width + window_chrome_width;
    int target_window_height = target_surface_height + window_chrome_height;

    // If native size does not fit, scale the video uniformly to the largest
    // available client area. Never enlarge a smaller source frame.
    const float scale = std::min({
        1.0f,
        maximum_window_width / static_cast<float>(target_window_width),
        maximum_window_height / static_cast<float>(target_window_height)
    });
    target_surface_width = std::max(1, qRound(target_surface_width * scale));
    target_surface_height = std::max(1, qRound(target_surface_height * scale));
    target_window_width = target_surface_width + window_chrome_width;
    target_window_height = target_surface_height + window_chrome_height;

    if (std::abs(target_window_width - width()) >= 8 ||
        std::abs(target_window_height - height()) >= 8)
        resize(target_window_width, target_window_height);

    const int maximum_x = available.right() + 1 - target_window_width;
    const int maximum_y = available.bottom() + 1 - target_window_height;
    QPoint adjusted_position = pos();
    adjusted_position.setX(std::clamp(
        adjusted_position.x(), available.left(), std::max(available.left(), maximum_x)));
    adjusted_position.setY(std::clamp(
        adjusted_position.y(), available.top(), std::max(available.top(), maximum_y)));
    if (adjusted_position != pos())
        move(adjusted_position);
}

void MainWindow::enableWindowResizeFrame()
{
#ifdef Q_OS_WIN
    if (mode_ != PlayerMode::Video)
        return;

    const HWND window = reinterpret_cast<HWND>(static_cast<quintptr>(winId()));
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if ((style & WS_THICKFRAME) == 0)
    {
        SetWindowLongPtrW(window, GWL_STYLE, style | WS_THICKFRAME);
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
}

void MainWindow::updateVideoOverlayGeometry()
{
    if (!video_surface_)
        return;

    const int surface_width = video_surface_->width();
    const int surface_height = video_surface_->height();

    if (title_bar_)
        title_bar_->setGeometry(
            6, 6, std::max(0, surface_width - 12), title_bar_->sizeHint().height());

    if (playlist_panel_)
        playlist_panel_->setGeometry(
            std::max(6, surface_width - playlist_panel_->width() - 6),
            6, playlist_panel_->width(), std::max(0, surface_height - 12));

    if (control_panel_)
    {
        const int panel_height = control_panel_->sizeHint().height();
        control_panel_->setGeometry(
            6, std::max(6, surface_height - panel_height - 6),
            std::max(0, surface_width - 12), panel_height);
    }
}

void MainWindow::updateFullscreenUi()
{
    fullscreen_hide_timer_->stop();

    if (isFullScreen())
    {
        if (menu_bar_)
            menu_bar_->hide();
        if (title_bar_)
            title_bar_->hide();
        if (statusBar())
            statusBar()->hide();
    }
    else
    {
        unsetCursor();
        if (title_bar_)
            title_bar_->hide();
        if (statusBar())
            statusBar()->setVisible(mode_ == PlayerMode::Audio);
    }

    if (title_bar_)
        title_bar_->hide();
    if (playlist_panel_)
        playlist_panel_->hide();
    if (control_panel_)
        control_panel_->hide();
    unsetCursor();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == playlist_widget_ && event->type() == QEvent::DragEnter)
    {
        auto* drag_event = static_cast<QDragEnterEvent*>(event);
        if (drag_event->mimeData()->hasUrls())
        {
            drag_event->acceptProposedAction();
            return true;
        }
    }
    if (watched == playlist_widget_ && event->type() == QEvent::Drop)
    {
        auto* drop_event = static_cast<QDropEvent*>(event);
        if (drop_event->mimeData()->hasUrls())
        {
            appendPlaylistFiles(drop_event->mimeData()->urls());
            drop_event->acceptProposedAction();
            return true;
        }
    }

    if (watched == seek_slider_ && event->type() == QEvent::MouseMove &&
        duration_ms_ > 0)
    {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        const int handle_width = style()->pixelMetric(
            QStyle::PM_SliderLength, nullptr, seek_slider_);
        const int margin = handle_width / 2;
        const int span = std::max(1, seek_slider_->width() - handle_width);
        const int position = std::clamp(
            mouse_event->position().toPoint().x() - margin, 0, span);
        const int value = QStyle::sliderValueFromPosition(
            seek_slider_->minimum(), seek_slider_->maximum(),
            position, span, seek_slider_->invertedAppearance());
        seek_slider_->setToolTip(format_position(duration_ms_ * value / 1000));
    }

    if (mode_ == PlayerMode::Video)
    {
        QWidget* watched_widget = qobject_cast<QWidget*>(watched);
        bool is_menu_widget = qobject_cast<QMenu*>(watched) != nullptr;
        for (QWidget* parent = watched_widget ? watched_widget->parentWidget() : nullptr;
            parent && !is_menu_widget; parent = parent->parentWidget())
            is_menu_widget = qobject_cast<QMenu*>(parent) != nullptr;
        const bool is_control_widget = watched == control_panel_ ||
            (control_panel_ && watched_widget && control_panel_->isAncestorOf(watched_widget)) ||
            watched == playlist_panel_ ||
            (playlist_panel_ && watched_widget && playlist_panel_->isAncestorOf(watched_widget)) ||
            watched == menu_bar_ || (menu_bar_ && watched_widget && menu_bar_->isAncestorOf(watched_widget)) ||
            watched == title_bar_ || (title_bar_ && watched_widget && title_bar_->isAncestorOf(watched_widget)) ||
            watched == statusBar() || (statusBar() && watched_widget && statusBar()->isAncestorOf(watched_widget)) ||
            is_menu_widget;
        if (watched == video_surface_ && event->type() == QEvent::Resize &&
            empty_state_label_ && empty_state_label_->isVisible())
        {
            empty_state_label_->adjustSize();
            empty_state_label_->move(
                (video_surface_->width() - empty_state_label_->width()) / 2,
                (video_surface_->height() - empty_state_label_->height()) / 2);
        }
        if (watched == video_surface_ && event->type() == QEvent::Resize)
        {
            if (osd_label_ && osd_label_->isVisible())
                positionOsd();
            updateVideoOverlayGeometry();
            if (playlist_panel_ && playlist_panel_->isVisible())
                playlist_panel_->raise();
            if (title_bar_ && title_bar_->isVisible())
                title_bar_->raise();
            if (control_panel_ && control_panel_->isVisible())
                control_panel_->raise();
        }
        if (event->type() == QEvent::Show && is_menu_widget)
        {
            fullscreen_hide_timer_->stop();
        }
        else if (event->type() == QEvent::Hide && is_menu_widget)
        {
            if (!QApplication::activePopupWidget() && fullscreen_hide_delay_ms_ > 0)
                fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
        }
        else if (event->type() == QEvent::MouseMove &&
                 (is_control_widget || watched == video_surface_))
        {
            unsetCursor();
            const QPoint position = video_surface_->mapFromGlobal(QCursor::pos());
            constexpr int reveal_zone = 48;
            const bool near_right_edge =
                position.x() >= video_surface_->width() - reveal_zone;
            const bool near_top_edge = position.y() <= reveal_zone;
            const bool near_bottom_edge =
                position.y() >= video_surface_->height() - reveal_zone;

            QWidget* target_panel = nullptr;
            if (near_right_edge && playlist_panel_visible_)
                target_panel = playlist_panel_;
            else if (near_top_edge)
                target_panel = title_bar_;
            else if (near_bottom_edge)
                target_panel = control_panel_;

            QWidget* active_panel = nullptr;
            if (title_bar_ && title_bar_->isVisible())
                active_panel = title_bar_;
            else if (playlist_panel_ && playlist_panel_->isVisible())
                active_panel = playlist_panel_;
            else if (control_panel_ && control_panel_->isVisible())
                active_panel = control_panel_;

            if (target_panel && target_panel != active_panel)
            {
                const QRect overlay_geometry = active_panel
                    ? active_panel->geometry()
                    : QRect();

                if (title_bar_ && title_bar_ != target_panel)
                    title_bar_->hide();
                if (playlist_panel_ && playlist_panel_ != target_panel)
                    playlist_panel_->hide();
                if (control_panel_ && control_panel_ != target_panel)
                    control_panel_->hide();

                if (active_panel)
                    target_panel->setGeometry(overlay_geometry);
                else
                {
                    updateVideoOverlayGeometry();
                }

                target_panel->show();
                target_panel->raise();
                if (fullscreen_hide_delay_ms_ > 0)
                    fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
                else
                    fullscreen_hide_timer_->stop();
            }
            else if (active_panel)
            {
                fullscreen_hide_timer_->stop();
            }
            else if (fullscreen_hide_delay_ms_ > 0 &&
                     !fullscreen_hide_timer_->isActive())
            {
                fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
            }
        }
        else if (event->type() == QEvent::Enter && is_control_widget)
        {
            unsetCursor();
            fullscreen_hide_timer_->stop();
        }
        else if (event->type() == QEvent::Leave && is_control_widget)
        {
            QWidget* cursor_widget = QApplication::widgetAt(QCursor::pos());
            const bool pointer_over_controls = cursor_widget &&
                ((control_panel_ &&
                    (cursor_widget == control_panel_ || control_panel_->isAncestorOf(cursor_widget))) ||
                 (playlist_panel_ &&
                    (cursor_widget == playlist_panel_ || playlist_panel_->isAncestorOf(cursor_widget))) ||
                 (menu_bar_ &&
                    (cursor_widget == menu_bar_ || menu_bar_->isAncestorOf(cursor_widget))) ||
                 (title_bar_ &&
                    (cursor_widget == title_bar_ || title_bar_->isAncestorOf(cursor_widget))) ||
                 (statusBar() &&
                    (cursor_widget == statusBar() || statusBar()->isAncestorOf(cursor_widget))));
            if (!pointer_over_controls && !QApplication::activePopupWidget() &&
                fullscreen_hide_delay_ms_ > 0)
                fullscreen_hide_timer_->start(fullscreen_hide_delay_ms_);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
    {
        if (!isFullScreen() && !isMinimized())
            normal_geometry_ = saveGeometry();
        if (title_bar_)
        {
            if (auto* maximize_button = title_bar_->findChild<QPushButton*>(
                QStringLiteral("maximizeWindowButton")))
            {
                maximize_button->setIcon(PlayerIcons::make(
                    isMaximized() ? PlayerIcons::Name::Restore : PlayerIcons::Name::Maximize,
                    QColor(QStringLiteral("#9aa6b5"))));
                maximize_button->setIconSize(QSize(18, 18));
                maximize_button->setToolTip(isMaximized()
                    ? QStringLiteral("还原") : QStringLiteral("最大化"));
                maximize_button->setAccessibleName(maximize_button->toolTip());
            }
        }
        updateFullscreenUi();
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (mode_ == PlayerMode::Video)
    {
        updateVideoOverlayGeometry();
        if (playlist_panel_ && playlist_panel_->isVisible())
            playlist_panel_->raise();
        if (title_bar_ && title_bar_->isVisible())
            title_bar_->raise();
        if (control_panel_ && control_panel_->isVisible())
            control_panel_->raise();
    }
    if (!isFullScreen() && !isMinimized())
        normal_geometry_ = saveGeometry();
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    if (!isFullScreen() && !isMinimized())
        normal_geometry_ = saveGeometry();
}

bool MainWindow::nativeEvent(
    const QByteArray& event_type, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    auto* native_message = static_cast<MSG*>(message);
    if (mode_ == PlayerMode::Video &&
        native_message->message == WM_NCHITTEST && !isFullScreen())
    {
        const QPoint position = mapFromGlobal(QCursor::pos());
        if (!isMaximized())
        {
            constexpr int border = 6;
            const bool left = position.x() < border;
            const bool right = position.x() >= width() - border;
            const bool top = position.y() < border;
            const bool bottom = position.y() >= height() - border;
            if (left || right || top || bottom)
            {
                if (top && left)
                    *result = HTTOPLEFT;
                else if (top && right)
                    *result = HTTOPRIGHT;
                else if (bottom && left)
                    *result = HTBOTTOMLEFT;
                else if (bottom && right)
                    *result = HTBOTTOMRIGHT;
                else if (left)
                    *result = HTLEFT;
                else if (right)
                    *result = HTRIGHT;
                else if (top)
                    *result = HTTOP;
                else
                    *result = HTBOTTOM;
                return true;
            }
        }
        const QRect title_bar_rect = title_bar_
            ? QRect(title_bar_->mapTo(this, QPoint(0, 0)), title_bar_->size())
            : QRect();
        if (title_bar_ && title_bar_->isVisible() &&
            title_bar_rect.contains(position))
        {
            for (QWidget* child = childAt(position);
                child && child != title_bar_; child = child->parentWidget())
            {
                if (qobject_cast<QAbstractButton*>(child))
                    return QMainWindow::nativeEvent(event_type, message, result);
            }
            *result = HTCAPTION;
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(event_type, message, result);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    openFiles(event->mimeData()->urls());
    event->acceptProposedAction();
}
