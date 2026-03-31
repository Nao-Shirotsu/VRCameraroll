#pragma execution_character_set("utf-8")
#include "ImageFolderObserver.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <unordered_set>

static const std::vector<std::wstring> kImageExts = { L".png", L".jpg", L".jpeg", L".bmp" };

static bool IsImageFile(const std::filesystem::path& p) {
    auto ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    for (auto& e : kImageExts) if (ext == e) return true;
    return false;
}

// --- public ---

void ImageFolderObserver::Start(const std::filesystem::path& folder,
                                int initial_offset, ChangeCallback cb)
{
    m_folder         = folder;
    m_callback       = std::move(cb);
    m_current_offset = initial_offset;
    m_last_files     = ScanFolder(folder);
    m_running        = true;
    m_thread         = std::thread(&ImageFolderObserver::ThreadFunc, this);
}

void ImageFolderObserver::Stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void ImageFolderObserver::SetOffset(int offset) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_current_offset = offset;
    m_has_pending    = false;  // 保留中の再読み込みをキャンセル
}

bool ImageFolderObserver::Poll() {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_has_pending) return false;
    const int offset = m_pending_offset;
    m_has_pending    = false;
    m_current_offset = offset;  // 適用済みとしてオフセットを確定
    lock.unlock();

    m_callback(offset);
    return true;
}

// --- private ---

void ImageFolderObserver::ThreadFunc() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!m_running) break;

        auto new_files = ScanFolder(m_folder);  // ロック外でファイル列挙（遅い処理）

        std::lock_guard<std::mutex> lock(m_mutex);

        // 前回の変更がまだ未処理なら今回はスキップ
        if (m_has_pending) continue;

        if (new_files == m_last_files) continue;

        // 新規追加されたファイルを数える（old_files に存在しないもの）
        std::unordered_set<std::string> old_set;
        old_set.reserve(m_last_files.size());
        for (const auto& p : m_last_files) old_set.insert(p.string());

        int added = 0;
        for (const auto& p : new_files)
            if (old_set.find(p.string()) == old_set.end()) ++added;

        // offset == 0（最新ページ）なら 0 で再読み込み。
        // それ以外は追加枚数分だけずらして同じ画像を維持する。
        const int new_offset = (m_current_offset == 0) ? 0 : m_current_offset + added;

        m_last_files     = std::move(new_files);
        m_has_pending    = true;
        m_pending_offset = new_offset;

        printf("フォルダ変更を検知。再読み込み offset=%d\n", new_offset);
    }
}

/*static*/
std::vector<std::filesystem::path> ImageFolderObserver::ScanFolder(
    const std::filesystem::path& folder)
{
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder))
        return {};

    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& e : std::filesystem::directory_iterator(folder))
        if (e.is_regular_file() && IsImageFile(e.path()))
            entries.push_back(e);

    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) {
            return a.last_write_time() > b.last_write_time();
        });

    std::vector<std::filesystem::path> result;
    result.reserve(entries.size());
    for (const auto& e : entries) result.push_back(e.path());
    return result;
}
