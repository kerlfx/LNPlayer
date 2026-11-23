#pragma once

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QSvgRenderer>

namespace PlayerIcons
{

enum class Name
{
    AppLogo, Play, Pause, Stop, Previous, Next,
    Volume, VolumeMuted, VolumeUp, VolumeDown, AudioEffects, AudioTrack, Subtitles, Language, SubtitleList, AudioTrackList,
    Minimize, Maximize, Restore, Close, Fullscreen, ExitFullscreen,
    Add, Remove, Clear, MoveUp, MoveDown,
    Shuffle, Repeat, RepeatOne, History, Search,
    Settings, General, Playback, Interface, Video, Filters, Keyboard, Screenshot, Performance,
    Folder, External, Reset, Playlist, Menu, About, Help, More, OpenFile, OpenFolder, Download, Delete, Properties, PictureInPicture, Lock, Hide, NightMode
};

inline const char* resource_id(Name name)
{
    switch (name)
    {
    case Name::AppLogo: return "app_logo";
    case Name::Play: return "play";
    case Name::Pause: return "pause";
    case Name::Stop: return "stop";
    case Name::Previous: return "previous";
    case Name::Next: return "next";
    case Name::Volume: return "volume";
    case Name::VolumeMuted: return "volume_muted";
    case Name::VolumeUp: return "volume_up";
    case Name::VolumeDown: return "volume_down";
    case Name::AudioEffects: return "audio_effects";
    case Name::Language: return "language";
    case Name::SubtitleList: return "subtitle_list";
    case Name::AudioTrack: return "audio_track";
    case Name::Subtitles: return "subtitles";
    case Name::AudioTrackList: return "audio_track_list";
    case Name::Minimize: return "minimize";
    case Name::Maximize: return "maximize";
    case Name::Restore: return "restore";
    case Name::Close: return "close";
    case Name::Fullscreen: return "fullscreen";
    case Name::ExitFullscreen: return "exit_fullscreen";
    case Name::Add: return "add";
    case Name::Remove: return "remove";
    case Name::Clear: return "clear";
    case Name::MoveUp: return "move_up";
    case Name::MoveDown: return "move_down";
    case Name::Shuffle: return "shuffle";
    case Name::Repeat: return "repeat";
    case Name::RepeatOne: return "repeat_one";
    case Name::History: return "history";
    case Name::Search: return "search";
    case Name::Settings: return "settings";
    case Name::General: return "general";
    case Name::Playback: return "playback";
    case Name::Interface: return "interface";
    case Name::Video: return "video";
    case Name::Filters: return "filters";
    case Name::Keyboard: return "keyboard";
    case Name::Screenshot: return "screenshot";
    case Name::Performance: return "performance";
    case Name::Folder: return "open_folder";
    case Name::External: return "external";
    case Name::Reset: return "reset";
    case Name::Playlist: return "playlist";
    case Name::Menu: return "menu";
    case Name::About: return "about";
    case Name::Help: return "help";
    case Name::More: return "more";
    case Name::OpenFile: return "open_file";
    case Name::OpenFolder: return "open_folder";
    case Name::Download: return "download";
    case Name::Delete: return "delete";
    case Name::Properties: return "properties";
    case Name::PictureInPicture: return "picture_in_picture";
    case Name::Lock: return "lock";
    case Name::Hide: return "hide";
    case Name::NightMode: return "night_mode";
    }
    return "play";
}

inline const QByteArray& svg_data()
{
    static const QByteArray data = []
    {
        QFile file(QCoreApplication::applicationDirPath() + QStringLiteral("/ui/player_icons.svg"));
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray{};
        return file.readAll();
    }();
    return data;
}

inline QIcon make(Name name, const QColor& color = QColor(QStringLiteral("#f0f4fa")))
{
    constexpr qreal logical_size = 24.0;
    constexpr qreal pixel_ratio = 2.0;

    QByteArray data = svg_data();
    QSvgRenderer renderer(data);
    if (!renderer.isValid())
        return {};

    const auto render_icon = [&](const QColor& tint)
    {
        QByteArray tinted_data = data;
        tinted_data.replace("currentColor", tint.name(QColor::HexRgb).toUtf8());
        QSvgRenderer tinted_renderer(tinted_data);
        QPixmap pixmap(48, 48);
        pixmap.fill(Qt::transparent);
        pixmap.setDevicePixelRatio(pixel_ratio);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        tinted_renderer.render(&painter, QString::fromLatin1(resource_id(name)),
                               QRectF(0, 0, logical_size, logical_size));
        return pixmap;
    };

    QIcon icon;
    icon.addPixmap(render_icon(color), QIcon::Normal, QIcon::Off);
    icon.addPixmap(render_icon(QColor(QStringLiteral("#8fc7ff"))),
                   QIcon::Active, QIcon::Off);
    icon.addPixmap(render_icon(QColor(QStringLiteral("#8fc7ff"))),
                   QIcon::Active, QIcon::On);
    return icon;
}

} // namespace PlayerIcons
