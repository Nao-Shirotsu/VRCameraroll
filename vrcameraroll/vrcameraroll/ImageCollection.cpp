#pragma execution_character_set("utf-8")
#include "ImageCollection.h"
#include <algorithm>
#include <vector>
#include <cstdio>

static const std::vector<std::wstring> kImageExts = { L".png", L".jpg", L".jpeg", L".bmp" };

static bool IsImageFile(const std::filesystem::path& p) {
    auto ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    for (auto& e : kImageExts) if (ext == e) return true;
    return false;
}

ImageCollection::ImageCollection()
    : m_main_it(m_images.begin()) {}

void ImageCollection::LoadFromFolder(const std::filesystem::path& folder, int offset) {
    for (auto& img : m_images) img.Unload();

    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        printf("Folder not found: %s\n", folder.string().c_str());
        m_main_it = m_images.begin();
        return;
    }

    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && IsImageFile(entry.path()))
            entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(),
        [](const auto& a, const auto& b) {
            return a.last_write_time() > b.last_write_time();
        });

    m_folder      = folder;
    m_offset      = offset;
    m_total_count = (int)entries.size();

    int start = std::min(offset, (int)entries.size());
    int count = std::min(N, (int)entries.size() - start);
    for (int i = 0; i < count; ++i) {
        m_images[i].LoadFromFile(entries[start + i].path());
    }

    m_main_it = m_images.begin();
    printf("Loaded %d image(s) [offset=%d] from: %s\n", count, offset, folder.string().c_str());
}

void ImageCollection::LoadNewerPage() {
    if (m_offset == 0) return; // すでに最新ページ
    LoadFromFolder(m_folder, std::max(0, m_offset - N));
}

void ImageCollection::LoadOlderPage() {
    int new_offset = m_offset + N;
    // 仮読み込みして画像があるか確認
    int prev_offset = m_offset;
    LoadFromFolder(m_folder, new_offset);
    if (LoadedCount() == 0) {
        // 以上古い画像なし。元のページに戻す。
        LoadFromFolder(m_folder, prev_offset);
    }
}

int ImageCollection::LoadedCount() const {
    int n = 0;
    for (const auto& img : m_images) if (img.IsLoaded()) ++n;
    return n;
}
