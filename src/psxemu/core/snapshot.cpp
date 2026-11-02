// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "snapshot.h"

#include <zstd.h>

SnapshotBuffer SnapshotBuffer::compress() const
{
    SnapshotBuffer dst;
    dst.resize(ZSTD_compressBound(size()));
    auto const compressed_size =
        ZSTD_compress(dst.data(), dst.size(), this->data(), this->size(), 3);
    dst.resize(compressed_size);
    dst._decompressed_size = this->size();
    dst.shrink_to_fit();
    return dst;
}

SnapshotBuffer SnapshotBuffer::decompress() const
{
    SnapshotBuffer dst;
    dst.resize(_decompressed_size);
    ZSTD_decompress(dst.data(), dst.size(), this->data(), this->size());
    return dst;
}
