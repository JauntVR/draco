/*
* @file MeshCompression.cpp
*
* Copyright (c) 2016 Personify Inc.
*
* @brief
*   Implements mesh MeshCompression
*/

#include "psy_draco_decoder.h"
#include "draco/compression/decode.h"
#include "draco/attributes/point_attribute.h"

namespace psy
{
namespace draco
{

class MeshDecompression::Impl
{
public:
    Impl()
    {
        mpMesh.reset(new ::draco::Mesh());
        mpBuffer.reset(new ::draco::DecoderBuffer());
        mpDecoder.reset(new ::draco::Decoder());
    }

    ~Impl()
    {
        mpMesh.reset();
        mpBuffer.reset();
        mpDecoder.reset();
    }

    MeshDecompression::eStatus Run(const char* pCompressedData,
                                   const size_t compressedDataSizeInBytes)
    {
        std::cout << "MeshDecompression::Run() - Starting\n";
        mpBuffer->Init(pCompressedData, compressedDataSizeInBytes);

        const auto type_statusor = ::draco::Decoder::GetEncodedGeometryType(mpBuffer.get());
        mStatus = type_statusor.status();
        if (!type_statusor.ok())
        {   
            return eStatus::FAILED;
        }

        const ::draco::EncodedGeometryType geom_type = type_statusor.value();
        if (geom_type != ::draco::TRIANGULAR_MESH)
        {
            mStatus = ::draco::Status(::draco::Status::Code::ERROR,
                                      "invalid encoded geometry type\n");
            return eStatus::FAILED;
        }

        auto statusor = mpDecoder->DecodeMeshFromBuffer(mpBuffer.get());
        if (!statusor.ok())
        {
            mStatus = statusor.status();
            return eStatus::FAILED;
        }

        mpMesh = std::move(statusor).value();
        ;
        std::cout << "MeshDecompression::Run() - Ending";
        return eStatus::SUCCEED;
    }

    void UpdateGeometryAttributeValues(const ::draco::PointAttribute* pPointAttribute,
                                       uint8_t* pValues,
                                       const size_t stride,
                                       const size_t verticesCount) const
    {
        const size_t data_sz_in_bytes = pPointAttribute->byte_stride();
        const auto src_stride = pPointAttribute->byte_stride();
        uint8_t* p_dst = pValues;
        if (pPointAttribute->is_mapping_identity())
        {
            const uint8_t* p_src = pPointAttribute->buffer()->data();
            if (stride == src_stride)
            {
                memcpy(p_dst, p_src, verticesCount * data_sz_in_bytes);
            }
            else
            {
                for (size_t i = 0; i < verticesCount; ++i, p_dst += stride, p_src += src_stride)
                {
                    memcpy(p_dst, p_src, data_sz_in_bytes);
                }
            }
        }
        else
        {
            ::draco::PointIndex pt_idx(0);
            for (size_t i = 0; i < verticesCount; ++i, pt_idx++, p_dst += stride)
            {
                memcpy(p_dst, pPointAttribute->GetAddressOfMappedIndex(pt_idx), data_sz_in_bytes);
            }
        }
    }

    std::vector<int> GetConnectivityVsAttributeSize()
    {
        return mpDecoder->GetConnectivityVsAttributeData();
    }

    void GetMesh(float* pVertices,
                 const size_t vertexStride,
                 unsigned int* pIndices,
                 unsigned char* pVisibilityAttributes) const
    {
        // update faces
        {
            const auto faces_count = mpMesh->num_faces();
            ::draco::FaceIndex face_index(0);
            for (int32_t i = 0, j = 0; i < faces_count; i++, face_index++)
            {
                const auto& face = mpMesh->face(face_index);
                pIndices[j++] = static_cast<unsigned int>(face[0].value());
                pIndices[j++] = static_cast<unsigned int>(face[1].value());
                pIndices[j++] = static_cast<unsigned int>(face[2].value());
            }
        }

        // update vertices
        {
            const int pos_att_id = mpMesh->GetNamedAttributeId(::draco::GeometryAttribute::POSITION);
            assert(pos_att_id >= 0);
            UpdateGeometryAttributeValues(mpMesh->attribute(pos_att_id),
                                          reinterpret_cast<uint8_t*>(pVertices),
                                          vertexStride,
                                          mpMesh->num_points());
        }

        // update visibility attribute
        if (nullptr != pVisibilityAttributes)
        {
            const int vis_att_id = mpMesh->GetNamedAttributeId(::draco::GeometryAttribute::GENERIC);
            assert(vis_att_id >= 0);
            UpdateGeometryAttributeValues(mpMesh->attribute(vis_att_id),
                                          pVisibilityAttributes,
                                          sizeof(uint8_t),
                                          mpMesh->num_points());
        }
    }

    std::unique_ptr<::draco::Mesh> mpMesh;
    std::unique_ptr<::draco::DecoderBuffer> mpBuffer;
    std::unique_ptr<::draco::Decoder> mpDecoder;
    ::draco::Status mStatus;
};

MeshDecompression:: MeshDecompression()
{
    mpImpl = new Impl();
}

MeshDecompression::~MeshDecompression()
{
    if (mpImpl)
    {
        delete mpImpl;
    }
    mpImpl = nullptr;
}

MeshDecompression::eStatus MeshDecompression::Run(const char* pCompressedData,
                                                  const size_t compressedDataSizeInBytes)
{
    return mpImpl->Run(pCompressedData, compressedDataSizeInBytes);
}

void MeshDecompression::GetMesh(float* pVertices,
                                const size_t vertexStride,
                                unsigned int* pIndices,
                                unsigned char* pVisibilityAttributes) const
{
    if (mpImpl->mStatus.ok())
    {
        mpImpl->GetMesh(pVertices,
                        vertexStride,
                        pIndices,
                        pVisibilityAttributes);
    }
}

size_t MeshDecompression::GetVerticesCount() const
{
    if (mpImpl->mStatus.ok())
    {
        return static_cast<size_t>(mpImpl->mpMesh->num_points());
    }
    return 0;
}

size_t MeshDecompression::GetFacesCount() const
{
    if (mpImpl->mStatus.ok())
    {
        return static_cast<size_t>(mpImpl->mpMesh->num_faces());
    }
    return 0;
}

const char* MeshDecompression::GetLastErrorMessage() const
{
    return mpImpl->mStatus.error_msg();
}

std::vector<int> MeshDecompression::GetConnectivityVsAttributeSize() const
{
    return mpImpl->GetConnectivityVsAttributeSize();
}

}; // namespace draco
}; // namespace psy
