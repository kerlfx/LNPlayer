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

void MainWindow::openFile()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("打开媒体文件"),
        QString(),
        QStringLiteral("媒体文件 (*.mp3 *.flac *.wav *.aac *.m4a *.ogg *.opus *.wma *.aiff *.aif *.mp4 *.mkv *.mov *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.m4v *.ts *.mts *.m2ts *.3gp *.3g2 *.vob *.ogv *.mxf *.asf *.f4v *.divx *.mpe *.mpv);;所有文件 (*)"));

    QList<QUrl> urls;
    for (const QString& file : files)
        urls.append(QUrl::fromLocalFile(file));

    openFiles(urls);
}

void MainWindow::openFiles(const QList<QUrl>& urls)
{
    if (urls.isEmpty())
        return;

    savePlaybackPosition();
    const uint64_t operation_generation = ++playlist_operation_generation_;
    status_label_->setText(QStringLiteral("正在扫描媒体文件..."));
    const auto tracker = task_tracker_;
    postLibrary([this, tracker, urls, operation_generation]
    {
        const QStringList paths = expand_media_urls(urls, tracker);
        QMetaObject::invokeMethod(this, [this, paths, operation_generation]
        {
            if (operation_generation != playlist_operation_generation_)
                return;
            QList<QUrl> next_playlist;
            for (const QString& path : paths)
                next_playlist.append(QUrl::fromLocalFile(path));
            if (next_playlist.isEmpty())
            {
                status_label_->setText(QStringLiteral("没有找到支持的媒体文件"));
                return;
            }

            for (const QString& queued_path : playlist_probe_queue_)
                playlist_probe_pending_.remove(queued_path);
            playlist_probe_queue_.clear();
            playlist_ = std::move(next_playlist);
            playlist_index_ = 0;
            resetShuffleOrder();
            rebuildPlaylistWidget();
            saveVideoPlaylist();
            openPlaylistIndex(0);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::appendPlaylistFiles(const QList<QUrl>& urls)
{
    if (urls.isEmpty())
        return;

    const uint64_t operation_generation = playlist_operation_generation_;
    const auto tracker = task_tracker_;
    postLibrary([this, tracker, urls, operation_generation]
    {
        const QStringList paths = expand_media_urls(urls, tracker);
        QMetaObject::invokeMethod(this, [this, paths, operation_generation]
        {
            if (operation_generation != playlist_operation_generation_)
                return;
            for (const QString& path : paths)
            {
                playlist_.append(QUrl::fromLocalFile(path));
                schedulePlaylistMetadataProbe(path);
            }
            resetShuffleOrder();
            rebuildPlaylistWidget();
            saveVideoPlaylist();
            if (playlist_index_ < 0 && !playlist_.isEmpty())
            {
                playlist_index_ = 0;
                resetShuffleOrder();
                openPlaylistIndex(0);
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::rebuildPlaylistWidget()
{
    if (!playlist_widget_)
        return;
    playlist_item_index_.clear();
    playlist_widget_->setUpdatesEnabled(false);
    playlist_widget_->clear();
    for (const QUrl& url : playlist_)
    {
        const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
        const QFileInfo file_info(path);
        auto* item = new QListWidgetItem(file_info.fileName(), playlist_widget_);
        item->setData(Qt::UserRole, path);
        playlist_item_index_.insert(path, item);
        updatePlaylistItemMetadata(path);
        if (!playlist_video_metadata_.contains(path))
            schedulePlaylistMetadataProbe(path);
    }
    playing_playlist_item_ = QPersistentModelIndex{};
    if (playlist_index_ >= 0 && playlist_index_ < playlist_.size())
    {
        playlist_widget_->setCurrentRow(playlist_index_);
        playing_playlist_item_ = playlist_widget_->model()->index(playlist_index_, 0);
    }
    playlist_widget_->setUpdatesEnabled(true);
    playlist_widget_->viewport()->update();
}

void MainWindow::schedulePlaylistMetadataProbe(const QString& path)
{
    if (path.isEmpty() || playlist_video_metadata_.contains(path) ||
        playlist_probe_pending_.contains(path))
        return;

    playlist_probe_pending_.insert(path);
    playlist_probe_queue_.push_back(path);
    startNextPlaylistMetadataProbe();
}

void MainWindow::startNextPlaylistMetadataProbe()
{
    if (playlist_probe_running_ || playlist_probe_queue_.empty())
        return;

    const QString path = std::move(playlist_probe_queue_.front());
    playlist_probe_queue_.pop_front();
    const QByteArray path_utf8 = path.toUtf8();
    playlist_probe_running_ = true;
    postLibrary([this, path, path_utf8]
    {
        lnplayer_video_info_t info{};
        info.struct_size = sizeof(info);
        int64_t duration_ms = -1;
        const lnplayer_result_t result = lnplayer_probe_media(
            path_utf8.constData(), &duration_ms, &info);
        QMetaObject::invokeMethod(this, [this, path, info, duration_ms, result]
        {
            playlist_probe_pending_.remove(path);
            if (result == LNPLAYER_RESULT_OK)
            {
                playlist_video_metadata_.insert(path, info);
                playlist_durations_.insert(path, duration_ms);
            }
            updatePlaylistItemMetadata(path);
            playlist_probe_running_ = false;
            startNextPlaylistMetadataProbe();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updatePlaylistItemMetadata(const QString& path)
{
    if (!playlist_widget_)
        return;

    const auto items = playlist_item_index_.values(path);
    if (items.isEmpty())
        return;

    const QFileInfo file_info(path);
    const auto metadata = playlist_video_metadata_.constFind(path);
    QString duration_text = QStringLiteral("--:--");
    QString resolution_text = QStringLiteral("未知");
    QString details = QStringLiteral("%1 bytes")
        .arg(file_info.exists() ? file_info.size() : 0);
    if (metadata != playlist_video_metadata_.cend())
    {
        const lnplayer_video_info_t& info = metadata.value();
        const int64_t duration = playlist_durations_.value(path, -1);
        if (duration >= 0)
            duration_text = format_position(duration);
        if (info.width > 0 && info.height > 0)
            resolution_text = QStringLiteral("%1×%2").arg(info.width).arg(info.height);
        details += QStringLiteral("\n容器：%1\n编码：%2\n像素格式：%3\n码率：%4")
            .arg(QString::fromUtf8(info.container_long_name))
            .arg(QString::fromUtf8(info.codec_name))
            .arg(QString::fromUtf8(info.pixel_format))
            .arg(info.bit_rate > 0
                ? QString::number(info.bit_rate / 1000) + QStringLiteral(" kbps")
                : QStringLiteral("未知"));
    }

    for (QListWidgetItem* item : items)
    {
        item->setText(file_info.fileName());
        item->setData(Qt::UserRole + 1, duration_text);
        item->setData(Qt::UserRole + 2, resolution_text);
        item->setToolTip(QStringLiteral("%1\n%2").arg(file_info.absoluteFilePath(), details));
    }
    playlist_widget_->viewport()->update();
}

void MainWindow::loadVideoPlaylist()
{
    QSettings settings(settings_file_path(), QSettings::IniFormat);
    const QStringList files = settings.value(QStringLiteral("VideoPlaylist/Files")).toStringList();
    playlist_.clear();
    for (const QString& path : files)
    {
        if (QFileInfo::exists(path))
        {
            playlist_.append(QUrl::fromLocalFile(path));
            schedulePlaylistMetadataProbe(QFileInfo(path).absoluteFilePath());
        }
    }

    playlist_index_ = playlist_.isEmpty()
        ? -1
        : std::clamp(
            settings.value(QStringLiteral("VideoPlaylist/CurrentIndex"), 0).toInt(),
            0, static_cast<int>(playlist_.size()) - 1);
    shuffle_enabled_ = settings.value(QStringLiteral("VideoPlaylist/Shuffle"), false).toBool();
    repeat_mode_ = std::clamp(
        settings.value(QStringLiteral("VideoPlaylist/RepeatMode"), 0).toInt(), 0, 2);
    if (shuffle_button_)
    {
        shuffle_button_->setChecked(shuffle_enabled_);
        shuffle_button_->setIcon(PlayerIcons::make(PlayerIcons::Name::Shuffle,
            shuffle_enabled_ ? QColor(QStringLiteral("#d7dde7"))
                : QColor(QStringLiteral("#8f99a8"))));
        shuffle_button_->setToolTip(shuffle_enabled_
            ? QStringLiteral("随机播放：开") : QStringLiteral("随机播放：关"));
        shuffle_button_->setAccessibleName(shuffle_button_->toolTip());
    }
    if (repeat_button_)
    {
        repeat_button_->setIcon(PlayerIcons::make(
            repeat_mode_ == 2 ? PlayerIcons::Name::RepeatOne : PlayerIcons::Name::Repeat,
            repeat_mode_ == 0 ? QColor(QStringLiteral("#8f99a8"))
                : QColor(QStringLiteral("#d7dde7"))));
        repeat_button_->setToolTip(repeat_mode_ == 0 ? QStringLiteral("循环播放：关")
            : repeat_mode_ == 1 ? QStringLiteral("循环播放：列表") : QStringLiteral("循环播放：单项"));
        repeat_button_->setAccessibleName(repeat_button_->toolTip());
    }
    resetShuffleOrder();
    rebuildPlaylistWidget();
    if (playlist_index_ >= 0)
        openPlaylistIndex(playlist_index_);
}

QString MainWindow::playbackPositionSettingKey(const QString& path) const
{
    const QByteArray normalized_path = QFileInfo(path).absoluteFilePath().toUtf8();
    const QByteArray digest = QCryptographicHash::hash(
        normalized_path, QCryptographicHash::Sha1).toHex();
    return QStringLiteral("Video/PlaybackPositions/") + QString::fromLatin1(digest);
}

QString MainWindow::mediaPreferenceKey(const QString& category, const QString& path) const
{
    const QByteArray normalized_path = QFileInfo(path).absoluteFilePath().toCaseFolded().toUtf8();
    const QByteArray digest = QCryptographicHash::hash(
        normalized_path, QCryptographicHash::Sha1).toHex();
    return QStringLiteral("Video/MediaPreferences/") + category +
        QStringLiteral("/") + QString::fromLatin1(digest);
}

void MainWindow::savePlaybackPosition() const
{
    if (!remember_position_ || mode_ != PlayerMode::Video || current_media_path_.isEmpty())
        return;

    const QString path = current_media_path_;
    if (path.isEmpty())
        return;

    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(playbackPositionSettingKey(path), current_position_ms_);
    settings.sync();
}

void MainWindow::saveVideoPlaylist() const
{
    if (mode_ != PlayerMode::Video)
        return;

    QSettings settings(settings_file_path(), QSettings::IniFormat);
    QStringList files;
    for (const QUrl& url : playlist_)
        files.append(QFileInfo(url.toLocalFile()).absoluteFilePath());
    settings.setValue(QStringLiteral("VideoPlaylist/Files"), files);
    settings.setValue(QStringLiteral("VideoPlaylist/CurrentIndex"), playlist_index_);
    settings.setValue(QStringLiteral("VideoPlaylist/Shuffle"), shuffle_enabled_);
    settings.setValue(QStringLiteral("VideoPlaylist/RepeatMode"), repeat_mode_);
    settings.sync();
}

void MainWindow::clearPlaylist()
{
    ++playlist_operation_generation_;
    savePlaybackPosition();
    for (const QString& queued_path : playlist_probe_queue_)
        playlist_probe_pending_.remove(queued_path);
    playlist_probe_queue_.clear();
    playlist_.clear();
    playlist_index_ = -1;
    current_media_path_.clear();
    shuffle_order_.clear();
    playing_playlist_item_ = QPersistentModelIndex{};
    playlist_open_pending_ = false;
    video_seek_pending_ = false;
    video_end_of_stream_ = false;
    has_video_ = false;
    has_audio_ = false;
    is_playing_ = false;
    duration_ms_ = -1;
    current_position_ms_ = 0;
    video_width_ = 0;
    video_height_ = 0;
    if (seek_slider_)
    {
        seek_slider_->setEnabled(false);
        seek_slider_->setValue(0);
    }
    video_first_frame_received_ = false;
    video_frame_history_.clear();
    video_frame_history_index_ = -1;
    video_width_ = 0;
    video_height_ = 0;
    pending_video_frame_valid_ = false;
    pending_hardware_frame_owner_.reset();
    pending_hardware_texture_ = nullptr;
    video_timer_->stop();

    playlist_item_index_.clear();
    if (playlist_widget_)
        playlist_widget_->clear();
    if (video_surface_)
        video_surface_->set_frame(QImage());
    if (empty_state_label_)
    {
        empty_state_label_->setText(
            QStringLiteral("拖放视频文件到此处，或从“文件”菜单打开视频"));
        empty_state_label_->adjustSize();
        empty_state_label_->move(
            (video_surface_->width() - empty_state_label_->width()) / 2,
            (video_surface_->height() - empty_state_label_->height()) / 2);
        empty_state_label_->show();
        empty_state_label_->raise();
    }
    media_title_label_->setText(QStringLiteral("未打开媒体"));
    position_label_->setText(QStringLiteral("00:00 / --:--"));
    status_label_->setText(QStringLiteral("播放列表已清空"));
    statusBar()->showMessage(QStringLiteral("播放列表已清空"));

    postCommand([this]
    {
        if (player_)
            lnplayer_close(player_);
    });
    saveVideoPlaylist();
}

void MainWindow::showPlaylistWindow()
{
    if (mode_ != PlayerMode::Video || !playlist_widget_)
        return;

    if (playlist_dialog_)
    {
        playlist_dialog_->show();
        playlist_dialog_->raise();
        playlist_dialog_->activateWindow();
        return;
    }

    auto* dialog = new QDialog(this);
    playlist_dialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("LNPlayer 播放列表"));
    dialog->resize(460, 560);

    auto* root = new QVBoxLayout(dialog);
    auto* view = new QListView(dialog);
    view->setItemDelegate(new PlaylistItemDelegate(view));
    view->setModel(playlist_widget_->model());
    root->addWidget(create_playlist_header(dialog));
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setDragDropMode(QAbstractItemView::InternalMove);
    view->setDropIndicatorShown(true);
    view->setDefaultDropAction(Qt::MoveAction);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(view, 1);

    auto* actions = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("添加文件"), dialog);
    auto* remove = new QPushButton(QStringLiteral("移除选中项"), dialog);
    auto* clear = new QPushButton(QStringLiteral("清空队列"), dialog);
    actions->addWidget(add);
    actions->addWidget(remove);
    actions->addWidget(clear);
    root->addLayout(actions);

    connect(view, &QAbstractItemView::doubleClicked, this,
        [this](const QModelIndex& index)
        {
            if (index.isValid())
                openPlaylistIndex(index.row());
        });
    connect(add, &QPushButton::clicked, dialog, [this]
    {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("添加到播放队列"), QString(),
            QStringLiteral("视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.wmv *.flv *.mpeg *.mpg *.m4v *.ts *.mts *.m2ts *.3gp *.3g2 *.vob *.ogv *.mxf *.asf *.f4v *.divx *.mpe *.mpv);;所有文件 (*)"));
        QList<QUrl> urls;
        for (const QString& file : files)
            urls.append(QUrl::fromLocalFile(file));
        appendPlaylistFiles(urls);
    });
    connect(remove, &QPushButton::clicked, dialog, [this, view]
    {
        playlist_widget_->clearSelection();
        for (const QModelIndex& index : view->selectionModel()->selectedRows())
        {
            if (QListWidgetItem* item = playlist_widget_->item(index.row()))
                item->setSelected(true);
        }
        removeSelectedPlaylistItem();
    });
    connect(clear, &QPushButton::clicked, dialog, [this]
    {
        clearPlaylist();
    });
    connect(dialog, &QObject::destroyed, this, [this]
    {
        playlist_dialog_ = nullptr;
    });

    dialog->show();
}

void MainWindow::removeSelectedPlaylistItem()
{
    savePlaybackPosition();
    if (!playlist_widget_)
        return;

    std::vector<int> rows;
    const auto selected_items = playlist_widget_->selectedItems();
    for (QListWidgetItem* item : selected_items)
        rows.push_back(playlist_widget_->row(item));
    if (rows.empty() && playlist_widget_->currentRow() >= 0)
        rows.push_back(playlist_widget_->currentRow());
    if (rows.empty())
        return;

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    const int old_current = playlist_index_;
    const bool removing_current = std::binary_search(rows.begin(), rows.end(), old_current);
    const int first_removed = rows.front();
    const int removed_before_current = static_cast<int>(
        std::count_if(rows.begin(), rows.end(), [old_current](int row)
        {
            return row < old_current;
        }));

    for (auto it = rows.rbegin(); it != rows.rend(); ++it)
    {
        if (*it >= 0 && *it < playlist_.size())
            playlist_.removeAt(*it);
    }

    if (removing_current)
    {
        if (playlist_.isEmpty())
        {
            clearPlaylist();
            return;
        }
        openPlaylistIndex(std::min(first_removed, static_cast<int>(playlist_.size()) - 1));
        return;
    }

    if (old_current >= 0)
        playlist_index_ = old_current - removed_before_current;
    resetShuffleOrder();
    rebuildPlaylistWidget();
    saveVideoPlaylist();
}

void MainWindow::moveSelectedPlaylistItem(int offset)
{
    if (!playlist_widget_)
        return;
    const int from = playlist_widget_->currentRow();
    const int to = from + offset;
    if (from < 0 || to < 0 || from >= playlist_.size() || to >= playlist_.size())
        return;

    playlist_.swapItemsAt(from, to);
    if (playlist_index_ == from)
        playlist_index_ = to;
    else if (playlist_index_ == to)
        playlist_index_ = from;
    resetShuffleOrder();
    rebuildPlaylistWidget();
    playlist_widget_->setCurrentRow(to);
    saveVideoPlaylist();
}

void MainWindow::cycleRepeatMode()
{
    repeat_mode_ = (repeat_mode_ + 1) % 3;
    if (repeat_button_)
    {
        repeat_button_->setIcon(PlayerIcons::make(
            repeat_mode_ == 2 ? PlayerIcons::Name::RepeatOne : PlayerIcons::Name::Repeat,
            repeat_mode_ == 0 ? QColor(QStringLiteral("#8f99a8"))
                : QColor(QStringLiteral("#d7dde7"))));
        repeat_button_->setToolTip(repeat_mode_ == 0 ? QStringLiteral("循环播放：关")
            : repeat_mode_ == 1 ? QStringLiteral("循环播放：列表") : QStringLiteral("循环播放：单项"));
        repeat_button_->setAccessibleName(repeat_button_->toolTip());
    }
    saveVideoPlaylist();
}

void MainWindow::resetShuffleOrder()
{
    shuffle_order_.clear();
    if (!shuffle_enabled_)
    {
        saveVideoPlaylist();
        return;
    }

    for (int index = 0; index < playlist_.size(); ++index)
        shuffle_order_.append(index);

    for (int index = shuffle_order_.size() - 1; index > 0; --index)
    {
        const int other = static_cast<int>(QRandomGenerator::global()->bounded(
            static_cast<quint32>(index + 1)));
        shuffle_order_.swapItemsAt(index, other);
    }

    if (playlist_index_ >= 0 && shuffle_order_.size() > 1)
    {
        const int current = shuffle_order_.indexOf(playlist_index_);
        if (current > 0)
            shuffle_order_.swapItemsAt(0, current);
    }
    saveVideoPlaylist();
}

int MainWindow::nextPlaylistIndex(bool manual) const
{
    if (playlist_.isEmpty() || playlist_index_ < 0)
        return -1;

    if (shuffle_enabled_)
    {
        const int current = shuffle_order_.indexOf(playlist_index_);
        if (current < 0)
            return -1;
        if (current + 1 < shuffle_order_.size())
            return shuffle_order_[current + 1];
        return (manual || repeat_mode_ == 1) && !shuffle_order_.isEmpty()
            ? shuffle_order_.front()
            : -1;
    }

    const int next = playlist_index_ + 1;
    if (next < playlist_.size())
        return next;
    return manual || repeat_mode_ == 1 ? 0 : -1;
}

void MainWindow::openPlaylistIndex(int index)
{
    if (index < 0 || index >= playlist_.size())
        return;

    savePlaybackPosition();
    playlist_index_ = index;
    if (mode_ == PlayerMode::Video)
    {
        QSettings settings(settings_file_path(), QSettings::IniFormat);
        QStringList recent_files = settings.value(
            QStringLiteral("Video/RecentFiles")).toStringList();
        const QString recent_path = QFileInfo(playlist_[index].toLocalFile()).absoluteFilePath();
        recent_files.removeAll(recent_path);
        recent_files.prepend(recent_path);
        while (recent_files.size() > 12)
            recent_files.removeLast();
        settings.setValue(QStringLiteral("Video/RecentFiles"), recent_files);
        settings.sync();
    }
    saveVideoPlaylist();
    if (playlist_widget_)
    {
        playlist_widget_->setCurrentRow(index);
        playing_playlist_item_ = playlist_widget_->model()->index(index, 0);
    }

    const QString source = playlist_[index].toLocalFile();
    if (source.isEmpty())
        return;
    current_media_path_ = QFileInfo(source).absoluteFilePath();
    has_video_ = false;
    has_audio_ = false;
    video_width_ = 0;
    video_height_ = 0;
    is_playing_ = false;
    duration_ms_ = -1;
    current_position_ms_ = 0;
    video_info_ = {};
    if (seek_slider_)
    {
        seek_slider_->setEnabled(false);
        seek_slider_->setValue(0);
    }

    subtitle_manager_.clear();
    loop_a_ms_ = -1;
    loop_b_ms_ = -1;
    viewing_historical_frame_ = false;
    video_seek_pending_ = false;
    pending_embedded_subtitle_.clear();
    pending_embedded_subtitle_bitmap_ = {};
    pending_video_display_aspect_ratio_ = 0.0f;
    pending_hdr_transfer_ = 0;
    pending_color_primaries_ = 0;
    pending_hdr_peak_luminance_ = 1000.0f;
    pending_video_ycbcr_matrix_ = 0;
    pending_video_ycbcr_full_range_ = 0;
    video_frame_history_.clear();
    video_frame_history_index_ = -1;
    pending_video_frame_valid_ = false;
    pending_hardware_frame_owner_.reset();
    pending_hardware_texture_ = nullptr;
    video_first_frame_received_ = false;
    video_end_of_stream_ = false;
    video_timer_->stop();
    if (video_surface_)
        video_surface_->set_frame(QImage());

    media_title_label_->setText(QFileInfo(source).fileName());
    if (empty_state_label_)
    {
        empty_state_label_->setText(QStringLiteral("正在打开视频..."));
        empty_state_label_->adjustSize();
        empty_state_label_->move(
            (video_surface_->width() - empty_state_label_->width()) / 2,
            (video_surface_->height() - empty_state_label_->height()) / 2);
        empty_state_label_->show();
        empty_state_label_->raise();
    }
    status_label_->setText(QStringLiteral("正在打开：%1").arg(source));
    statusBar()->showMessage(QStringLiteral("正在打开媒体"));
    playlist_open_pending_ = true;

    const QByteArray source_utf8 = source.toUtf8();
    postCommand([this, source_utf8]
    {
        if (!player_)
            return;

        uint64_t operation_id = 0;
        const lnplayer_result_t result = lnplayer_open(
            player_,
            source_utf8.constData(),
            &operation_id);

        if (result != LNPLAYER_RESULT_OK)
        {
            QMetaObject::invokeMethod(this, [this]
            {
                playlist_open_pending_ = false;
                status_label_->setText(QStringLiteral("媒体打开请求失败"));
            }, Qt::QueuedConnection);
        }
    });
}

void MainWindow::playPrevious()
{
    if (playlist_.isEmpty() || playlist_index_ < 0)
        return;

    if (shuffle_enabled_)
    {
        const int current = shuffle_order_.indexOf(playlist_index_);
        if (current < 0 || shuffle_order_.isEmpty())
            return;
        const int previous = current > 0 ? current - 1 : shuffle_order_.size() - 1;
        openPlaylistIndex(shuffle_order_[previous]);
        return;
    }

    const int previous = playlist_index_ > 0
        ? playlist_index_ - 1
        : playlist_.size() - 1;
    openPlaylistIndex(previous);
}

void MainWindow::playNext()
{
    const int next = nextPlaylistIndex(true);
    if (next >= 0)
        openPlaylistIndex(next);
}

void MainWindow::togglePlayback()
{
    if (video_seek_pending_)
        return;
    const bool should_pause = is_playing_;
    const bool resume_from_history = !should_pause && viewing_historical_frame_;
    const int64_t resume_position = current_position_ms_;
    if (resume_from_history)
    {
        viewing_historical_frame_ = false;
        video_frame_history_.clear();
        video_frame_history_index_ = -1;
        pending_video_frame_valid_ = false;
        video_end_of_stream_ = false;
        video_seek_pending_ = true;
        seek_slider_->setEnabled(false);
        video_timer_->start(0);
    }

    postCommand([this, should_pause, resume_from_history, resume_position]
    {
        if (!player_)
        {
            if (resume_from_history)
                QMetaObject::invokeMethod(this, [this]
                {
                    video_seek_pending_ = false;
                    seek_slider_->setEnabled(duration_ms_ > 0);
                }, Qt::QueuedConnection);
            return;
        }
        if (should_pause)
        {
            lnplayer_pause(player_);
            return;
        }
        if (resume_from_history)
        {
            uint64_t operation_id = 0;
            if (lnplayer_seek(player_, resume_position, &operation_id) != LNPLAYER_RESULT_OK)
            {
                QMetaObject::invokeMethod(this, [this]
                {
                    video_seek_pending_ = false;
                    seek_slider_->setEnabled(duration_ms_ > 0);
                    requestVideoFrame();
                }, Qt::QueuedConnection);
                return;
            }
        }
        lnplayer_play(player_);
    });
}

void MainWindow::seekRelative(int64_t offset_ms)
{
    if (duration_ms_ <= 0 || video_seek_pending_)
        return;
    const QString message = offset_ms < 0
        ? QStringLiteral("后退 %1 秒").arg(-offset_ms / 1000)
        : QStringLiteral("前进 %1 秒").arg(offset_ms / 1000);
    seekToPosition(current_position_ms_ + offset_ms, message);
}

void MainWindow::seekToPosition(int64_t position_ms, const QString& message)
{
    if (video_seek_pending_)
        return;

    showOsd(message);
    viewing_historical_frame_ = false;
    video_seek_pending_ = true;
    seek_slider_->setEnabled(false);
    video_frame_history_.clear();
    video_frame_history_index_ = -1;
    pending_video_frame_valid_ = false;
    video_end_of_stream_ = false;
    video_timer_->start(0);
    const int64_t target = duration_ms_ > 0
        ? std::clamp<int64_t>(position_ms, 0, duration_ms_)
        : std::max<int64_t>(0, position_ms);
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
}

void MainWindow::setPlaybackRate(float rate)
{
    if (is_playing_ && video_clock_timer_.isValid())
    {
        video_clock_anchor_position_ms_ = video_clock_anchor_position_ms_ +
            static_cast<int64_t>(std::llround(
                video_clock_timer_.nsecsElapsed() / 1000000.0 * playback_rate_));
        video_clock_timer_.restart();
    }
    else
    {
        video_clock_anchor_position_ms_ = current_position_ms_;
    }
    playback_rate_ = rate;
    if (pending_video_frame_valid_)
        scheduleVideoPresentation();
    showOsd(QStringLiteral("播放速度 %1x").arg(rate, 0, 'f', 2));
    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Video/PlaybackRate"), rate);
    settings.sync();

    postCommand([this, rate]
    {
        if (player_)
            lnplayer_set_playback_rate(player_, rate);
    });
}

void MainWindow::handleCoreEvent(
    lnplayer_event_type_t type,
    lnplayer_result_t result,
    lnplayer_state_t state,
    int64_t position_ms,
    int64_t duration_ms,
    uint64_t generation,
    const QString& message)
{
    if (generation < media_generation_)
        return;
    media_generation_ = generation;
    updatePlaybackState(state, position_ms, duration_ms);

    switch (type)
    {
    case LNPLAYER_EVENT_OPEN_COMPLETED:
        playlist_open_pending_ = false;
        if (result == LNPLAYER_RESULT_OK)
        {
            video_info_ = {};
            postCommand([this]
            {
                lnplayer_video_info_t info{};
                info.struct_size = sizeof(info);
                const lnplayer_result_t info_result = player_
                    ? lnplayer_get_video_info(player_, &info)
                    : LNPLAYER_RESULT_INVALID_HANDLE;
                if (info_result == LNPLAYER_RESULT_OK)
                {
                    QMetaObject::invokeMethod(this, [this, info]
                    {
                        video_info_ = info;
                    }, Qt::QueuedConnection);
                }
            });
            status_label_->setText(QStringLiteral("媒体已打开"));
            statusBar()->showMessage(QStringLiteral("就绪"));
            seek_slider_->setEnabled(duration_ms > 0);
            if (mode_ == PlayerMode::Video && remember_position_ &&
                playlist_index_ >= 0 && playlist_index_ < playlist_.size())
            {
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                const QString key = playbackPositionSettingKey(
                    playlist_[playlist_index_].toLocalFile());
                const int64_t restore_position = settings.value(key, 0).toLongLong();
                if (restore_position > 5000 &&
                    (duration_ms <= 0 || restore_position < duration_ms - 5000))
                {
                    video_seek_pending_ = true;
                    seek_slider_->setEnabled(false);
                    postCommand([this, restore_position]
                    {
                        uint64_t operation_id = 0;
                        const lnplayer_result_t seek_result = player_
                            ? lnplayer_seek(player_, restore_position, &operation_id)
                            : LNPLAYER_RESULT_INVALID_HANDLE;
                        if (seek_result != LNPLAYER_RESULT_OK)
                        {
                            QMetaObject::invokeMethod(this, [this]
                            {
                                video_seek_pending_ = false;
                                statusBar()->showMessage(QStringLiteral("恢复播放位置失败"));
                            }, Qt::QueuedConnection);
                        }
                    });
                }
            }
            if (playlist_index_ >= 0 && playlist_index_ < playlist_.size())
            {
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                const int saved_audio_track = settings.value(
                    mediaPreferenceKey(QStringLiteral("AudioTrack"), current_media_path_), -1).toInt();
                if (saved_audio_track >= 0)
                    selectAudioTrack(saved_audio_track);
            }
            if (playlist_index_ >= 0 && playlist_index_ < playlist_.size())
            {
                QSettings settings(settings_file_path(), QSettings::IniFormat);
                const QString external_subtitle_key = mediaPreferenceKey(
                    QStringLiteral("ExternalSubtitle"), current_media_path_);
                const QString external_subtitle_path = settings.value(external_subtitle_key).toString();
                if (!external_subtitle_path.isEmpty() && QFileInfo::exists(external_subtitle_path))
                {
                    loadExternalSubtitlePath(external_subtitle_path, false);
                }
                else
                {
                    if (!external_subtitle_path.isEmpty())
                    {
                        settings.remove(external_subtitle_key);
                        settings.sync();
                    }
                    const QString subtitle_track_key = mediaPreferenceKey(
                        QStringLiteral("SubtitleTrack"), current_media_path_);
                    if (settings.contains(subtitle_track_key))
                    {
                        const int saved_subtitle_track = settings.value(subtitle_track_key).toInt();
                        selectSubtitleTrack(saved_subtitle_track);
                    }
                    else if (auto_match_subtitle_)
                    {
                        const QFileInfo media_info(playlist_[playlist_index_].toLocalFile());
                        const QString base_name = media_info.completeBaseName();
                        const QStringList subtitle_extensions = {
                            QStringLiteral(".srt"), QStringLiteral(".ass"), QStringLiteral(".ssa")
                        };
                        for (const QString& extension : subtitle_extensions)
                        {
                            const QString candidate = QDir(media_info.absolutePath())
                                .filePath(base_name + extension);
                            if (QFileInfo::exists(candidate))
                            {
                                loadExternalSubtitlePath(candidate, false);
                                break;
                            }
                        }
                    }
                }
            }
            snapshot_timer_->setInterval(mode_ == PlayerMode::Video ? 25 : 250);
            snapshot_timer_->start();
            refreshSnapshot();
            pending_video_frame_valid_ = false;
            pending_hardware_frame_owner_.reset();
            pending_hardware_texture_ = nullptr;
            video_first_frame_received_ = false;
            video_end_of_stream_ = false;
            last_video_frame_ = {};
            if (video_surface_)
                video_surface_->set_frame(QImage());
        }
        else
        {
            status_label_->setText(message.isEmpty()
                ? QStringLiteral("媒体打开失败")
                : message);
            statusBar()->showMessage(QStringLiteral("打开失败"));
            if (empty_state_label_)
            {
                empty_state_label_->setText(message.isEmpty()
                    ? QStringLiteral("无法打开此视频文件")
                    : message);
                empty_state_label_->adjustSize();
                empty_state_label_->move(
                    (video_surface_->width() - empty_state_label_->width()) / 2,
                    (video_surface_->height() - empty_state_label_->height()) / 2);
                empty_state_label_->show();
                empty_state_label_->raise();
            }
        }
        break;

    case LNPLAYER_EVENT_SEEK_COMPLETED:
        if (mode_ == PlayerMode::Video)
        {
            video_seek_pending_ = false;
            seek_slider_->setEnabled(duration_ms_ > 0);
            pending_video_frame_valid_ = false;
            pending_hardware_frame_owner_.reset();
            pending_hardware_texture_ = nullptr;
            video_first_frame_received_ = false;
            video_end_of_stream_ = false;
            requestVideoFrame();
        }
        break;

    case LNPLAYER_EVENT_PLAYBACK_ENDED:
    {
        if (playlist_open_pending_ || video_seek_pending_)
            break;
        if (repeat_mode_ == 2 && playlist_index_ >= 0)
        {
            openPlaylistIndex(playlist_index_);
            break;
        }
        if (!auto_play_next_ && repeat_mode_ != 1)
        {
            video_timer_->stop();
            status_label_->setText(QStringLiteral("播放结束"));
            break;
        }
        const int next_index = nextPlaylistIndex(false);
        if (next_index >= 0)
        {
            openPlaylistIndex(next_index);
            break;
        }
        video_timer_->stop();
        status_label_->setText(QStringLiteral("播放结束"));
        break;
    }

    case LNPLAYER_EVENT_ERROR:
        status_label_->setText(message.isEmpty()
            ? QStringLiteral("播放发生错误")
            : message);
        break;

    case LNPLAYER_EVENT_STATE_CHANGED:
    default:
        break;
    }
}

void MainWindow::refreshSnapshot()
{
    if (snapshot_pending_)
        return;
    snapshot_pending_ = true;

    postBackground([this]
    {
        lnplayer_snapshot_t snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        lnplayer_t* player = player_.load(std::memory_order_acquire);
        const lnplayer_result_t result = player
            ? lnplayer_get_snapshot(player, &snapshot)
            : LNPLAYER_RESULT_INVALID_HANDLE;

        QMetaObject::invokeMethod(this, [this, result, snapshot]
        {
            snapshot_pending_ = false;
            if (result != LNPLAYER_RESULT_OK)
                return;

            media_generation_ = std::max(media_generation_, snapshot.generation);
            has_audio_ = (snapshot.reserved & LNPLAYER_MEDIA_HAS_AUDIO) != 0;
            has_video_ = (snapshot.reserved & LNPLAYER_MEDIA_HAS_VIDEO) != 0;
            updatePlaybackState(snapshot.state, snapshot.position_ms, snapshot.duration_ms);

            if (mode_ == PlayerMode::Video && !has_video_ &&
                !playlist_open_pending_ && !current_media_path_.isEmpty() && empty_state_label_)
            {
                empty_state_label_->setText(QStringLiteral("当前媒体没有可播放的视频流"));
                empty_state_label_->adjustSize();
                empty_state_label_->move(
                    (video_surface_->width() - empty_state_label_->width()) / 2,
                    (video_surface_->height() - empty_state_label_->height()) / 2);
                empty_state_label_->show();
                empty_state_label_->raise();
            }
            if (has_video_ && !video_first_frame_received_ &&
                !pending_video_frame_valid_ && !video_frame_pending_)
                requestVideoFrame();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::updatePlaybackState(
    lnplayer_state_t state,
    int64_t position_ms,
    int64_t duration_ms)
{
    duration_ms_ = duration_ms;
    if (viewing_historical_frame_ && state == LNPLAYER_STATE_PAUSED &&
        video_frame_history_index_ >= 0 &&
        video_frame_history_index_ < static_cast<int>(video_frame_history_.size()))
        position_ms = video_frame_history_[video_frame_history_index_].pts_ms;
    else if (state != LNPLAYER_STATE_PAUSED)
        viewing_historical_frame_ = false;
    current_position_ms_ = position_ms;

    if (mode_ == PlayerMode::Video && state == LNPLAYER_STATE_PLAYING &&
        loop_a_ms_ >= 0 && loop_b_ms_ > loop_a_ms_ &&
        position_ms >= loop_b_ms_ && !video_seek_pending_)
    {
        video_seek_pending_ = true;
        const int64_t loop_target = loop_a_ms_;
        postCommand([this, loop_target]
        {
            uint64_t operation_id = 0;
            const lnplayer_result_t result = player_
                ? lnplayer_seek(player_, loop_target, &operation_id)
                : LNPLAYER_RESULT_INVALID_HANDLE;
            if (result != LNPLAYER_RESULT_OK)
            {
                QMetaObject::invokeMethod(this, [this]
                {
                    video_seek_pending_ = false;
                    loop_a_ms_ = -1;
                    loop_b_ms_ = -1;
                    statusBar()->showMessage(QStringLiteral("A-B 循环定位失败，已关闭区间循环"));
                }, Qt::QueuedConnection);
            }
        });
    }

    const bool was_playing = is_playing_;
    is_playing_ = state == LNPLAYER_STATE_PLAYING;
    play_button_->setIcon(PlayerIcons::make(
        is_playing_ ? PlayerIcons::Name::Pause : PlayerIcons::Name::Play));
    play_button_->setToolTip(is_playing_ ? QStringLiteral("暂停") : QStringLiteral("播放"));
    play_button_->setAccessibleName(play_button_->toolTip());
    video_clock_anchor_position_ms_ = position_ms;
    if (is_playing_)
        video_clock_timer_.restart();
    else
        video_clock_timer_.invalidate();
    if (is_playing_ && pending_video_frame_valid_)
        scheduleVideoPresentation();
    if (mode_ == PlayerMode::Video && has_video_)
    {
        if (is_playing_)
        {
            if (!was_playing)
                video_end_of_stream_ = false;
            if (!video_timer_->isActive())
                video_timer_->start(0);
        }
        else
        {
            video_timer_->stop();
            presentPendingVideoFrame();
        }
    }

    const QString duration_text = duration_ms < 0
        ? QStringLiteral("--:--")
        : format_position(duration_ms);
    position_label_->setText(
        QStringLiteral("%1 / %2")
            .arg(format_position(position_ms), duration_text));

    if (!slider_dragging_ && duration_ms > 0)
    {
        const int value = static_cast<int>(
            std::clamp<int64_t>(position_ms * 1000 / duration_ms, 0, 1000));
        seek_slider_->setValue(value);
    }

    switch (state)
    {
    case LNPLAYER_STATE_OPENING:
        status_label_->setText(QStringLiteral("正在打开"));
        break;
    case LNPLAYER_STATE_READY:
        status_label_->setText(QStringLiteral("已就绪"));
        break;
    case LNPLAYER_STATE_PLAYING:
        status_label_->setText(QStringLiteral("正在播放"));
        break;
    case LNPLAYER_STATE_PAUSED:
        status_label_->setText(QStringLiteral("已暂停"));
        break;
    case LNPLAYER_STATE_ENDED:
        status_label_->setText(QStringLiteral("播放结束"));
        break;
    case LNPLAYER_STATE_ERROR:
        status_label_->setText(QStringLiteral("播放错误"));
        break;
    case LNPLAYER_STATE_CLOSING:
        status_label_->setText(QStringLiteral("正在关闭"));
        break;
    case LNPLAYER_STATE_IDLE:
    case LNPLAYER_STATE_SEEKING:
    default:
        break;
    }
}

void MainWindow::loadExternalSubtitle()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载外挂字幕"),
        QString(),
        QStringLiteral("字幕文件 (*.srt *.ass *.ssa);;SubRip (*.srt);;ASS/SSA (*.ass *.ssa);;所有文件 (*)"));
    if (!path.isEmpty())
        loadExternalSubtitlePath(path);
}

void MainWindow::loadExternalSubtitlePath(const QString& path, bool remember)
{
    const QByteArray path_utf8 = path.toUtf8();
    const QString media_path = current_media_path_;
    postCommand([this, path, path_utf8, media_path, remember]
    {
        const lnplayer_result_t result = player_
            ? lnplayer_set_external_subtitle(player_, path_utf8.constData())
            : LNPLAYER_RESULT_INVALID_HANDLE;
        if (result != LNPLAYER_RESULT_OK && player_)
            lnplayer_set_external_subtitle(player_, "");

        if (result == LNPLAYER_RESULT_OK)
        {
            QMetaObject::invokeMethod(this, [this, path, media_path, remember]
            {
                if (current_media_path_ != media_path)
                    return;
                subtitle_manager_.clear();
                status_label_->setText(
                    QStringLiteral("已加载外挂字幕：%1").arg(QFileInfo(path).fileName()));
                if (remember && !media_path.isEmpty())
                {
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        mediaPreferenceKey(QStringLiteral("ExternalSubtitle"), media_path), path);
                    settings.remove(
                        mediaPreferenceKey(QStringLiteral("SubtitleTrack"), media_path));
                    settings.sync();
                }
                pending_video_frame_valid_ = false;
                requestVideoFrame();
            }, Qt::QueuedConnection);
            return;
        }

        postBackground([this, path, media_path, remember]
        {
            auto fallback_manager = std::make_shared<SubtitleManager>();
            QString error;
            const bool loaded = fallback_manager->load_file(path, error);
            QMetaObject::invokeMethod(this,
                [this, path, media_path, remember, fallback_manager, error, loaded]
            {
                if (current_media_path_ != media_path)
                    return;
                if (!loaded)
                {
                    status_label_->setText(error);
                    return;
                }

                subtitle_manager_ = std::move(*fallback_manager);
                status_label_->setText(
                    QStringLiteral("已加载外挂字幕（基础文本回退）：%1")
                        .arg(QFileInfo(path).fileName()));
                if (remember && !media_path.isEmpty())
                {
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        mediaPreferenceKey(QStringLiteral("ExternalSubtitle"), media_path), path);
                    settings.remove(
                        mediaPreferenceKey(QStringLiteral("SubtitleTrack"), media_path));
                    settings.sync();
                }
                pending_video_frame_valid_ = false;
                requestVideoFrame();
            }, Qt::QueuedConnection);
        });
    });
}

void MainWindow::selectSubtitleTrack(int stream_index)
{
    const QString media_path = current_media_path_;
    postCommand([this, stream_index, media_path]
    {
        if (!player_)
            return;

        lnplayer_snapshot_t snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        if (lnplayer_get_snapshot(player_, &snapshot) != LNPLAYER_RESULT_OK)
            return;

        const bool resume_playback = snapshot.state == LNPLAYER_STATE_PLAYING;
        if (resume_playback && lnplayer_pause(player_) != LNPLAYER_RESULT_OK)
            return;

        const lnplayer_result_t result = lnplayer_select_subtitle_track(player_, stream_index);
        const lnplayer_result_t resume_result = resume_playback
            ? lnplayer_play(player_)
            : LNPLAYER_RESULT_OK;

        QMetaObject::invokeMethod(this,
            [this, result, resume_playback, resume_result, stream_index, media_path]
        {
            is_playing_ = resume_playback && resume_result == LNPLAYER_RESULT_OK;
            if (result == LNPLAYER_RESULT_OK)
            {
                if (!media_path.isEmpty())
                {
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        mediaPreferenceKey(QStringLiteral("SubtitleTrack"), media_path),
                        stream_index);
                    settings.remove(
                        mediaPreferenceKey(QStringLiteral("ExternalSubtitle"), media_path));
                    settings.sync();
                }
                pending_embedded_subtitle_.clear();
                pending_embedded_subtitle_bitmap_ = {};
                video_frame_history_.clear();
                video_frame_history_index_ = -1;
                pending_video_frame_valid_ = false;
                requestVideoFrame();
                showOsd(resume_result == LNPLAYER_RESULT_OK
                    ? QStringLiteral("内嵌字幕已切换")
                    : QStringLiteral("内嵌字幕已切换，但恢复播放失败"));
            }
            else
            {
                statusBar()->showMessage(QStringLiteral("内嵌字幕切换失败"));
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::selectAudioTrack(int stream_index)
{
    const QString media_path = current_media_path_;
    postCommand([this, stream_index, media_path]
    {
        if (!player_)
            return;

        lnplayer_snapshot_t snapshot{};
        snapshot.struct_size = sizeof(snapshot);
        if (lnplayer_get_snapshot(player_, &snapshot) != LNPLAYER_RESULT_OK)
            return;

        const bool resume_playback = snapshot.state == LNPLAYER_STATE_PLAYING;
        if (resume_playback && lnplayer_pause(player_) != LNPLAYER_RESULT_OK)
            return;

        const lnplayer_result_t result = lnplayer_select_audio_track(player_, stream_index);
        const lnplayer_result_t resume_result = resume_playback
            ? lnplayer_play(player_)
            : LNPLAYER_RESULT_OK;

        QMetaObject::invokeMethod(this,
            [this, result, resume_playback, resume_result, stream_index, media_path]
        {
            is_playing_ = resume_playback && resume_result == LNPLAYER_RESULT_OK;
            if (result == LNPLAYER_RESULT_OK)
            {
                if (!media_path.isEmpty())
                {
                    QSettings settings(settings_file_path(), QSettings::IniFormat);
                    settings.setValue(
                        mediaPreferenceKey(QStringLiteral("AudioTrack"), media_path),
                        stream_index);
                    settings.sync();
                }
                video_frame_history_.clear();
                video_frame_history_index_ = -1;
                pending_video_frame_valid_ = false;
                requestVideoFrame();
                showOsd(resume_result == LNPLAYER_RESULT_OK
                    ? QStringLiteral("音轨已切换")
                    : QStringLiteral("音轨已切换，但恢复播放失败"));
            }
            else
            {
                statusBar()->showMessage(QStringLiteral("音轨切换失败"));
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::setSubtitleOffset(int64_t offset_ms)
{
    subtitle_offset_ms_ = std::clamp<int64_t>(offset_ms, -60000, 60000);
    QSettings settings(settings_file_path(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Video/Subtitle/OffsetMs"), subtitle_offset_ms_);
    settings.sync();

    const int64_t applied_offset = subtitle_offset_ms_;
    postCommand([this, applied_offset]
    {
        if (player_)
            lnplayer_set_subtitle_offset(player_, applied_offset);
        QMetaObject::invokeMethod(this, [this, applied_offset]
        {
            pending_video_frame_valid_ = false;
            requestVideoFrame();
            showOsd(QStringLiteral("字幕偏移：%1 ms").arg(applied_offset));
        }, Qt::QueuedConnection);
    });
}

QString MainWindow::subtitleAt(int64_t position_ms) const
{
    return subtitle_manager_.text_at(
        position_ms, subtitle_offset_ms_, subtitles_enabled_);
}
