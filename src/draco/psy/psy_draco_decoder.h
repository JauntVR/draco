/*
* @file psy_draco_decoder.h
*
* Copyright (c) 2017 Personify Inc.
*
* @brief
*   Interface to mesh decompression
*/

#ifndef PSY_DRACO_MESH_DECOMPRESSION_H
#define PSY_DRACO_MESH_DECOMPRESSION_H

#include <memory>
#include "psy_draco.h"

namespace psy
{
namespace draco
{

/* A simple wrapper for draco decoder */
class PSY_DRACO_API MeshDecompression
{
public:
    enum eStatus
    {
        SUCCEED = 0,
        FAILED
    };

    MeshDecompression();
    ~MeshDecompression();

    eStatus Run(const char* pCompressedData,
                const size_t compressedDataSizeInBytes);

    size_t GetVerticesCount() const;
    size_t GetFacesCount() const;
    void GetMesh(float* pVertices,
                 const size_t vertexStride,
                 unsigned int* pIndices,
                 unsigned char* pVisibilityAttributes) const;

    const char* GetLastErrorMessage() const;

private:
    /* MeshCompression is non-copyable */
    MeshDecompression(const MeshDecompression&) {};
    MeshDecompression& operator=(const MeshDecompression&) { return *this; };

    class Impl;
    Impl* mpImpl;
}; // MeshCompression

}; // namespace draco
}; // namespace psy

#endif // PSY_DRACO_MESH_DECOMPRESSION_H
