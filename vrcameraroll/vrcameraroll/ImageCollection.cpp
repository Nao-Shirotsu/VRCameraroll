#pragma execution_character_set("utf-8")
#include "ImageCollection.h"
#include <algorithm>
#include <vector>
#include <cstdio>

static const std::vector<std::wstring> kImageExts = { L".png", L".jpg", L".jpeg", L".bmp" };

static bool IsImageFile(const std::filesystem::path& p) {
    auto ext = p.extension().wstring();
    // 小文字化して比較
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    for (auto& e : kImageExts) if (ext == e) return true;
    return false;
}

ImageCollection::ImageCollection()
    : m_main_it(m_images.begin()) {}

void ImageCollection::LoadFromFolder(const std::filesystem::path& folder) {
    // 全スロットをクリア
    for (auto& img : m_images) img.Unload();

    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        printf("Folder not found: %s\n", folder.string().c_str());
        m_main_it = m_images.begin();
        return;
    }

    // 画像ファイルを列挙してタイムスタンプ降順でソート
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && IsImageFile(entry.path()))
            entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) {
            return a.last_write_time() > b.last_write_time();
        });

    // 最大 N 枚を読み込む
    int count = std::min((int)entries.size(), N);
    for (int i = 0; i < count; ++i) {
        m_images[i].LoadFromFile(entries[i].path());
    }

    m_main_it = m_images.begin();
    printf("Loaded %d image(s) from: %s\n", count, folder.string().c_str());
}

int ImageCollection::LoadedCount() const {
    int n = 0;
    for (const auto& img : m_images) if (img.IsLoaded()) ++n;
    return n;
}
