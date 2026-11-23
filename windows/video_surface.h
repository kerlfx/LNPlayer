#ifndef LNPLAYER_VIDEO_SURFACE_H
#define LNPLAYER_VIDEO_SURFACE_H

#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QTimer>
#include <QPoint>
#include <QRect>

#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>

namespace lnplayer::windows
{
class D3D11VideoRenderer;
}

class VideoSurface final : public QWidget
{
public:
    explicit VideoSurface(QWidget* parent = nullptr);
    ~VideoSurface() override;

    void set_frame(const QImage& frame);
    bool has_renderer() const;
    void* native_device() const;
    void set_stretch(bool enabled);
    void set_gesture_enabled(bool enabled);
    bool consume_gesture_context_menu();
    void set_aspect_ratio(float ratio);
    void adjust_zoom(int direction);
    float zoom() const;
    QRect display_rect() const;
    void set_source_aspect_ratio(float ratio);
    void set_video_filter(
        float brightness, float contrast, float saturation, float hue_degrees,
        float sharpness, float denoise, bool grayscale);
    void set_hdr_metadata(int transfer, int color_primaries, float peak_luminance);
    void set_video_color_space(int ycbcr_matrix, int full_range);
    bool present_frame_threadsafe(const QImage& frame);
    bool present_hardware_frame(
        void* texture, uint32_t array_slice, uint32_t format,
        int width, int height, std::shared_ptr<void> frame_owner,
        const QImage& overlay);
    QImage capture_frame() const;
    void set_interaction_callbacks(
        std::function<void()> double_click,
        std::function<void(int, bool)> wheel,
        std::function<void()> middle_click,
        std::function<void(int)> gesture);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QImage frame_;
    mutable std::mutex frame_mutex_;
    lnplayer::windows::D3D11VideoRenderer* renderer_ = nullptr;
    void* native_window_ = nullptr;
    bool stretch_ = false;
    float aspect_ratio_ = 0.0f;
    float source_aspect_ratio_ = 0.0f;
    std::function<void()> double_click_callback_;
    std::function<void(int, bool)> wheel_callback_;
    std::function<void()> middle_click_callback_;
    std::function<void(int)> gesture_callback_;
    QPoint right_press_position_;
    QPoint left_press_position_;
    QPoint left_press_window_offset_;
    bool window_move_started_ = false;
    bool gesture_enabled_ = false;
    bool gesture_triggered_ = false;
    float zoom_ = 1.0f;
    QTimer resize_timer_;
    QSize pending_renderer_size_;
};

#endif
