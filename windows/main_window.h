#ifndef LNPLAYER_MAIN_WINDOW_H
#define LNPLAYER_MAIN_WINDOW_H

#include <QMainWindow>
#include <QSize>
#include <QDebug>
#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QList>
#include <QUrl>
#include <QHash>
#include <QSet>

#include <vector>
#include <deque>
#include <memory>
#include <atomic>
#include <cstdint>
#include <exception>
#include <utility>

#include "lnplayer.h"
#include "subtitle_manager.h"
#include "video_surface.h"
#include "ui_task_runtime.h"

class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QListView;
class QDialog;
class QListWidgetItem;
class QMenuBar;
class QObject;
class QEvent;
class QMoveEvent;
class QResizeEvent;
class QPushButton;
class QSlider;
class QShortcut;
class QTimer;
class QWidget;

enum class PlayerMode
{
    Audio,
    Video
};

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(PlayerMode mode, const QStringList& initial_media_files = QStringList(), QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result) override;

private:
    struct VideoFrameHistoryEntry
    {
        QImage image;
        std::shared_ptr<void> hardware_owner;
        void* hardware_texture = nullptr;
        uint32_t hardware_array_slice = 0;
        uint32_t hardware_format = 0;
        int hardware_width = 0;
        int hardware_height = 0;
        int hdr_transfer = 0;
        int color_primaries = 0;
        float hdr_peak_luminance = 1000.0f;
        float display_aspect_ratio = 0.0f;
        int ycbcr_matrix = 0;
        int ycbcr_full_range = 0;
        QString embedded_subtitle;
        QImage embedded_subtitle_bitmap;
        int64_t pts_ms = 0;
        uint64_t generation = 0;
    };

    void openFile();
    void loadExternalSubtitle();
    void loadExternalSubtitlePath(const QString& path, bool remember = true);
    QString subtitleAt(int64_t position_ms) const;
    void setSubtitleOffset(int64_t offset_ms);
    void selectAudioTrack(int stream_index);
    void selectSubtitleTrack(int stream_index);
    void openFiles(const QList<QUrl>& urls);
    void appendPlaylistFiles(const QList<QUrl>& urls);
    void rebuildPlaylistWidget();
    void schedulePlaylistMetadataProbe(const QString& path);
    void startNextPlaylistMetadataProbe();
    void updatePlaylistItemMetadata(const QString& path);
    void loadVideoPlaylist();
    void saveVideoPlaylist() const;
    void savePlaybackPosition() const;
    QString playbackPositionSettingKey(const QString& path) const;
    QString mediaPreferenceKey(const QString& category, const QString& path) const;
    void removeSelectedPlaylistItem();
    void clearPlaylist();
    void openVideoSettings();
    void showPlaylistWindow();
    void showOsd(const QString& message);
    void positionOsd();
    void showPlaybackInfo();
    void moveSelectedPlaylistItem(int offset);
    void cycleRepeatMode();
    void resetShuffleOrder();
    int nextPlaylistIndex(bool manual) const;
    void openPlaylistIndex(int index);
    void playPrevious();
    void playNext();
    void refreshLibrary();
    void requestVideoFrame();
    void scheduleVideoPresentation();
    void presentPendingVideoFrame();
    void togglePlayback();
    void seekRelative(int64_t offset_ms);
    void seekToPosition(int64_t position_ms, const QString& message);
    void takeScreenshot();
    void stepVideoFrame(bool backwards = false);
    void trimVideoFrameHistory();
    void presentHistoricalVideoFrame();
    void setPlaybackRate(float rate);
    void handleCoreEvent(
        lnplayer_event_type_t type,
        lnplayer_result_t result,
        lnplayer_state_t state,
        int64_t position_ms,
        int64_t duration_ms,
        uint64_t generation,
        const QString& message);
    void refreshSnapshot();
    void updatePlaybackState(lnplayer_state_t state, int64_t position_ms, int64_t duration_ms);
    void updateFullscreenUi();
    void enableWindowResizeFrame();
    void updateVideoOverlayGeometry();
    void fitWindowToVideoAspect(float aspect_ratio, const QSize& source_size);

    template <typename Executor, typename Handler>
    void postTask(const Executor& executor, Handler&& handler)
    {
        const auto tracker = task_tracker_;
        if (!tracker->try_begin())
            return;

        asio::post(dispatch_executor_,
            [executor, tracker, task = std::forward<Handler>(handler)]() mutable
            {
                asio::post(executor, [tracker, task = std::move(task)]() mutable
                {
                    struct Completion
                    {
                        std::shared_ptr<UiTaskTracker> tracker;
                        ~Completion()
                        {
                            tracker->finish();
                        }
                    } completion{tracker};
                    try
                    {
                        task();
                    }
                    catch (const std::exception& error)
                    {
                        qWarning() << "LNPlayer UI background task failed:" << error.what();
                    }
                    catch (...)
                    {
                        qWarning() << "LNPlayer UI background task failed with an unknown exception";
                    }
                });
            });
    }

    template <typename Handler>
    void postCommand(Handler&& handler)
    {
        postTask(command_executor_, std::forward<Handler>(handler));
    }

    template <typename Handler>
    void postFrame(Handler&& handler)
    {
        postTask(frame_executor_, std::forward<Handler>(handler));
    }

    template <typename Handler>
    void postLibrary(Handler&& handler)
    {
        postTask(library_executor_, std::forward<Handler>(handler));
    }

    template <typename Handler>
    void postBackground(Handler&& handler)
    {
        postTask(background_executor_, std::forward<Handler>(handler));
    }

    PlayerMode mode_;
    QLabel* status_label_;
    QLabel* position_label_;
    QLabel* media_title_label_;
    QLineEdit* library_search_;
    QListWidget* library_list_;
    QListWidget* playlist_widget_ = nullptr;
    QDialog* playlist_dialog_ = nullptr;
    VideoSurface* video_surface_ = nullptr;
    QWidget* control_panel_ = nullptr;
    QWidget* title_bar_ = nullptr;
    QMenuBar* menu_bar_ = nullptr;
    QLabel* osd_label_ = nullptr;
    QLabel* empty_state_label_ = nullptr;
    QWidget* playlist_panel_ = nullptr;
    QAction* playlist_visibility_action_ = nullptr;
    QAction* always_on_top_action_ = nullptr;
    QHash<QString, QShortcut*> shortcuts_;
    QSlider* seek_slider_;
    QSlider* volume_slider_;
    QPushButton* play_button_;
    QPushButton* stop_button_;
    QPushButton* shuffle_button_ = nullptr;
    QPushButton* repeat_button_ = nullptr;
    QPushButton* playlist_toggle_button_ = nullptr;
    std::shared_ptr<UiTaskTracker> task_tracker_;
    asio::strand<asio::io_context::executor_type> dispatch_executor_;
    asio::thread_pool::executor_type background_executor_;
    asio::strand<asio::thread_pool::executor_type> command_executor_;
    asio::strand<asio::thread_pool::executor_type> frame_executor_;
    asio::strand<asio::thread_pool::executor_type> library_executor_;
    QTimer* snapshot_timer_;
    QTimer* video_timer_;
    QTimer* device_timer_;
    QTimer* fullscreen_hide_timer_;
    QTimer* osd_timer_;
    QTimer* volume_settings_timer_;
    std::atomic<lnplayer_t*> player_{nullptr};
    void* video_device_identity_ = nullptr;
    int64_t duration_ms_ = -1;
    bool slider_dragging_ = false;
    bool snapshot_pending_ = false;
    bool playlist_panel_visible_ = true;
    int fullscreen_hide_delay_ms_ = 2000;
    int osd_duration_ms_ = 1500;
    int osd_position_ = 1;
    QString screenshot_directory_;
    QString screenshot_format_ = QStringLiteral("png");
    QString subtitle_font_family_ = QStringLiteral("Segoe UI");
    QString subtitle_color_ = QStringLiteral("#ffffff");
    int subtitle_font_size_ = 0;
    int subtitle_vertical_position_ = 80;
    int middle_click_action_ = 2;
    int double_click_action_ = 1;
    int wheel_action_ = 0;
    bool auto_match_subtitle_ = true;
    bool mouse_gestures_enabled_ = false;
    bool remember_position_ = true;
    bool auto_play_next_ = true;
    bool always_on_top_ = false;
    QByteArray normal_geometry_;
    int video_width_ = 0;
    int video_height_ = 0;
    lnplayer_video_info_t video_info_{};
    int64_t loop_a_ms_ = -1;
    int64_t loop_b_ms_ = -1;
    bool has_video_ = false;
    bool has_audio_ = false;
    bool is_playing_ = false;
    bool video_frame_pending_ = false;
    bool pending_video_frame_valid_ = false;
    bool video_first_frame_received_ = false;
    bool video_end_of_stream_ = false;
    bool video_seek_pending_ = false;
    bool viewing_historical_frame_ = false;
    QImage pending_video_frame_;
    float pending_video_display_aspect_ratio_ = 0.0f;
    int pending_hdr_transfer_ = 0;
    int pending_color_primaries_ = 0;
    float pending_hdr_peak_luminance_ = 1000.0f;
    int pending_video_ycbcr_matrix_ = 0;
    int pending_video_ycbcr_full_range_ = 0;
    QString pending_embedded_subtitle_;
    QImage pending_embedded_subtitle_bitmap_;
    std::shared_ptr<void> pending_hardware_frame_owner_;
    void* pending_hardware_texture_ = nullptr;
    uint32_t pending_hardware_array_slice_ = 0;
    uint32_t pending_hardware_format_ = 0;
    int pending_hardware_width_ = 0;
    int pending_hardware_height_ = 0;
    QImage last_video_frame_;
    QList<VideoFrameHistoryEntry> video_frame_history_;
    int video_frame_history_index_ = -1;
    int64_t pending_video_pts_ms_ = 0;
    uint64_t pending_video_generation_ = 0;
    int64_t current_position_ms_ = 0;
    int64_t video_clock_anchor_position_ms_ = 0;
    QElapsedTimer video_clock_timer_;
    int muted_volume_ = 1000;
    float playback_rate_ = 1.0f;
    float video_aspect_ratio_override_ = 0.0f;
    bool video_stretch_ = false;
    float video_brightness_ = 0.0f;
    float video_contrast_ = 1.0f;
    float video_saturation_ = 1.0f;
    float video_hue_degrees_ = 0.0f;
    float video_sharpness_ = 0.0f;
    float video_denoise_ = 0.0f;
    bool video_grayscale_ = false;
    uint64_t media_generation_ = 0;
    SubtitleManager subtitle_manager_;
    QList<QUrl> playlist_;
    QHash<QString, lnplayer_video_info_t> playlist_video_metadata_;
    QHash<QString, int64_t> playlist_durations_;
    QSet<QString> playlist_probe_pending_;
    std::deque<QString> playlist_probe_queue_;
    bool playlist_probe_running_ = false;
    int playlist_index_ = -1;
    uint64_t playlist_operation_generation_ = 0;
    QString current_media_path_;
    QListWidgetItem* playing_playlist_item_ = nullptr;
    QMultiHash<QString, QListWidgetItem*> playlist_item_index_;
    bool playlist_open_pending_ = false;
    bool subtitles_enabled_ = true;
    int64_t subtitle_offset_ms_ = 0;
    bool shuffle_enabled_ = false;
    int repeat_mode_ = 0;
    QList<int> shuffle_order_;
};

#endif
