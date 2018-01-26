/*
* @file psy_draco_encoder.h
*
* Copyright (c) 2017 Personify Inc.
*
* @brief
*   Interface to mesh compression
*/

#ifndef PSY_DRACO_MESH_COMPRESSION_H
#define PSY_DRACO_MESH_COMPRESSION_H

#include <memory>
#include "psy_draco.h"

namespace psy
{
namespace draco
{

/* A simple wrapper for draco encoder */
class PSY_DRACO_API MeshCompression
{
public:
    enum eStatus
    {
        SUCCEED = 0,
        FAILED
    };

    /*
    * - compressionLevel (compression level)
    *   + {0 = lowest, 10 = highest} compression ratio.
    * - vertexPositionQuantizationBitsCount (quantization parameter)
    *   + 0: not perform any quantization
    *   + ?: quantize the input values to that specified number of bits
    * - hasVisibilityInfo (compress visibility info of view ports per camers
    *   + determine existing visibility info to compress or not
    *   + supporting maximum 8 view ports, have been presented by corresponding bits
    *     1 for visible and 0 for invisible
    */
    MeshCompression(int compressionLevel = 7,
                    int vertexPositionQuantizationBitsCount = 10,
                    bool hasVisibilityInfo = false,
                    bool hasVerttexColorInfo = false);
    ~MeshCompression();

    void SetVertexPositionQuantizationBitsCount(const int);

    bool IsVisiblityInfoCompressing() const;

    bool IsVertexColorInfoCompressing() const;

    eStatus Run(const float* pVertices,
                const size_t vertexStride,
                const size_t verticesCount,
                const unsigned int* pIndices,
                const size_t indicesCount,
                const unsigned char* pVisibilityAttributes,
                const unsigned char* pVertexColorAttributes,
                const MeshType meshType = MeshType::FULL_MESH);

    const char* GetCompressedData() const;
    size_t GetCompressedDataSizeInBytes() const;

    const char* GetLastErrorMessage() const;

private:
    /* MeshCompression is non-copyable */
    MeshCompression(const MeshCompression&);
    MeshCompression& operator=(const MeshCompression&);

    class Impl;
    Impl* mpImpl;
}; // MeshCompression

}; // namespace draco
}; // namespace psy

#endif // PSY_DRACO_MESH_COMPRESSION_H
