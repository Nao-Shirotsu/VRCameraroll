#pragma once
#include "Image.h"
#include <array>
#include <filesystem>

class ImageCollection {
public:
    static constexpr int N = 4; // [0]=メイン, [1..3]=サブ
    using Iterator = std::array<Image, N>::iterator;

    ImageCollection();

    // folder 内の画像を更新日時の降順（最新順）で最大 N 枚読み込む。
    // 読み込み後、main iterator は先頭（最新画像）にリセットされる。
    void LoadFromFolder(const std::filesystem::path& folder);

    // メイン画像（大きく表示する対象）
    Image& Main()             { return *m_main_it; }
    const Image& Main() const { return *m_main_it; }

    // メイン画像の iterator を移動（端に達したらそれ以上進まない）
    void NextMain() { if (m_main_it + 1 != m_images.end() && (m_main_it + 1)->IsLoaded()) ++m_main_it; }
    void PrevMain() { if (m_main_it != m_images.begin()) --m_main_it; }

    void SetMain(Iterator it) { m_main_it = it; }
    Iterator MainIterator()   { return m_main_it; }

    // 全スロットへのアクセス（プレビュー描画用）
    std::array<Image, N>& Images()             { return m_images; }
    const std::array<Image, N>& Images() const { return m_images; }

    int LoadedCount() const;

private:
    std::array<Image, N> m_images;
    Iterator             m_main_it;
};
