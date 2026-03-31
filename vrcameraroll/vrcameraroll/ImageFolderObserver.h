#pragma once
#include <filesystem>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

// フォルダを 1 秒ごとにポーリングし、画像ファイルの増減を検知するデーモン。
// 変更を検知したら Poll() 経由でコールバックを発火する（メインスレッド側）。
//
// オフセット管理ルール:
//   - 現在 offset == 0（最新ページ）なら reload offset = 0
//   - offset > 0 なら reload offset = 現在 offset + 追加枚数（同じ画像を維持）
//
// ページ送り時は SetOffset() を呼ぶこと。
// 保留中の再読み込み要求はそのタイミングでキャンセルされ、次回スキャンで再計算される。
class ImageFolderObserver {
public:
    // 引数: 再読み込みすべき offset
    using ChangeCallback = std::function<void(int /*new_offset*/)>;

    ImageFolderObserver() = default;
    ~ImageFolderObserver() { Stop(); }

    // 監視開始。LoadImages() の直後に呼ぶ。
    void Start(const std::filesystem::path& folder, int initial_offset, ChangeCallback cb);

    void Stop();

    // ページ送り時に呼ぶ。保留中の再読み込みをキャンセルする。
    void SetOffset(int offset);

    // メインループから毎フレーム呼ぶ。変更があればコールバックを発火し true を返す。
    bool Poll();

private:
    void ThreadFunc();
    static std::vector<std::filesystem::path> ScanFolder(const std::filesystem::path& folder);

    ChangeCallback        m_callback;
    std::filesystem::path m_folder;

    std::mutex                         m_mutex;
    std::vector<std::filesystem::path> m_last_files;    // 最終スキャン結果（新しい順）
    int                                m_current_offset = 0;
    bool                               m_has_pending    = false;
    int                                m_pending_offset = 0;

    std::thread       m_thread;
    std::atomic<bool> m_running{ false };
};
