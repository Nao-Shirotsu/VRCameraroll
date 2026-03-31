#pragma once
#include "Image.h"
#include <array>
#include <filesystem>

class ImageCollection {
public:
    static constexpr int N = 7; // [0]=メイン候補含む全スロット

    using Iterator = std::array<Image, N>::iterator;

    ImageCollection();

    // folder 内の画像を更新日時の降順（最新順）で、offset 枚スキップして最大 N 枚読み込む。
    // 読み込み後、main iterator は先頭（最新画像）にリセットされる。
    void LoadFromFolder(const std::filesystem::path& folder, int offset = 0);

    // ← ボタン: N枚単位で新しいページを読み込む。最新ページにいる場合は何もしない。
    void LoadNewerPage();
    // → ボタン: N枚単位で古いページを読み込む。以上なければ何もしない。
    void LoadOlderPage();

    Image& Main()             { return *m_main_it; }
    const Image& Main() const { return *m_main_it; }

    void SetMain(Iterator it) { m_main_it = it; }
    Iterator MainIterator()   { return m_main_it; }

    std::array<Image, N>& Images()             { return m_images; }
    const std::array<Image, N>& Images() const { return m_images; }

    int LoadedCount() const;

    int GetOffset() const { return m_offset; }
    int GetTotalCount() const { return m_total_count; }

    // ページ端判定
    bool IsAtNewest() const { return m_offset == 0; }
    bool IsAtOldest() const { return m_total_count == 0 || m_offset + N >= m_total_count; }

private:
    std::array<Image, N> m_images;
    Iterator              m_main_it;
    std::filesystem::path m_folder;
    int                   m_offset      = 0;
    int                   m_total_count = 0;
};
