/*
* @file MeshCompression.cpp
*
* Copyright (c) 2016 Personify Inc.
*
* @brief
*   Implements mesh MeshCompression
*/

#include "psy_draco_decoder.h"
#include "draco/attributes/point_attribute.h"
#include "draco/compression/mesh/mesh_edgebreaker_decoder.h"

namespace psy
{
namespace draco
{

class MeshEdgeBreakerDecompression : protected ::draco::MeshEdgeBreakerDecoder
{
public:
    MeshEdgeBreakerDecompression() : ::draco::MeshEdgeBreakerDecoder()
    {
        mIsIncrementalDecompression = false;
        mVerticesCount = 0;
    }

    ::draco::Status Decompress(::draco::DecoderBuffer& rBuffer,
                               ::draco::Mesh& rMesh,
                               const bool isIncrementalDecompression)
    {
        mIsIncrementalDecompression = isIncrementalDecompression;
        const auto status = Decode(::draco::DecoderOptions(), &rBuffer, &rMesh);
        mVerticesCount = (status.ok() ? rMesh.num_points() : 0);
        return status;
    }
protected:
    bool InitializeDecoder() override
    {
        if (mIsIncrementalDecompression)
        {
            if (!mpDecoderState)
            {
                return false;
            }
            SetDecoderImplState(*mpDecoderState);
            return true;
        }
        return ::draco::MeshEdgeBreakerDecoder::InitializeDecoder();
    }

    bool DecodeConnectivity() override
    {
        if (mIsIncrementalDecompression)
        {
            point_cloud()->set_num_points(mVerticesCount);
            return true;
        }
        const auto status = ::draco::MeshEdgeBreakerDecoder::DecodeConnectivity();
        if (status)
        {
            mpDecoderState = std::move(CloneDecoderImplState());
        }
        return status;
    }
private:
    bool mIsIncrementalDecompression;
    size_t mVerticesCount;
    std::unique_ptr<::draco::MeshEdgeBreakerDecoderImplInterface> mpDecoderState;
};

class MeshDecompression::Impl
{
public:
    Impl()
    {
        mpBuffer.reset(new ::draco::DecoderBuffer());
        mpMesh.reset(new ::draco::Mesh());
        mpMeshDecompression.reset(new MeshEdgeBreakerDecompression());
    }

    ~Impl()
    {
        mpBuffer.reset();
        mpMesh.reset();
        mpMeshDecompression.reset();
    }

    MeshDecompression::eStatus Run(const char* pCompressedData,
                                   const size_t compressedDataSizeInBytes)
    {
        mpBuffer->Init(pCompressedData, compressedDataSizeInBytes);

        const bool is_incremental_decompression = false;

        // reset mesh
        mpMesh->set_num_points(0);
        if (false == is_incremental_decompression)
        {
            mpMesh->SetNumFaces(0);
        }
        for (auto i = mpMesh->num_attributes() - 1; i >= 0; --i)
        {
            mpMesh->DeleteAttribute(i);
        }

        mStatus = mpMeshDecompression->Decompress(*mpBuffer, *mpMesh, is_incremental_decompression);

        if (!mStatus.ok())
        {
            return eStatus::FAILED;
        }

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
            if (stride == static_cast<size_t>(src_stride))
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

    const ::draco::PointAttribute* GetPointAttributeByType(const ::draco::GeometryAttribute::Type type) const
    {
        const int attrib_id = mpMesh->GetNamedAttributeId(type);
        if (attrib_id >= 0)
        {
            return mpMesh->attribute(attrib_id);
        }
        return nullptr;
    }

    const ::draco::PointAttribute* GetVisibilityAttribute() const
    {
        return GetPointAttributeByType(::draco::GeometryAttribute::GENERIC);
    }

    const ::draco::PointAttribute* GetVertexColorAttribute() const
    {
        return GetPointAttributeByType(::draco::GeometryAttribute::COLOR);
    }

    const ::draco::PointAttribute* GetTexCoordAttribute() const
    {
        return GetPointAttributeByType(::draco::GeometryAttribute::TEX_COORD);
    }

    bool HasVisibilityInfo() const
    {
        return (nullptr != GetVisibilityAttribute());
    }

    bool HasVertexColorInfo() const
    {
        return (nullptr != GetVertexColorAttribute());
    }

    bool HasTexCoordInfo() const
    {
        return (nullptr != GetTexCoordAttribute());
    }

    void GetMesh(float* pVertices,
                 const size_t vertexStride,
                 unsigned int* pIndices,
                 unsigned char* pVisibilityAttributes,
                 unsigned char* pVertexColorAttributes,
                 unsigned char* pTexCoordAttributes) const
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
            UpdateGeometryAttributeValues(GetPointAttributeByType(::draco::GeometryAttribute::POSITION),
                                          reinterpret_cast<uint8_t*>(pVertices),
                                          vertexStride,
                                          mpMesh->num_points());
        }

        // update visibility attribute
        if (nullptr != pVisibilityAttributes && HasVisibilityInfo())
        {
            UpdateGeometryAttributeValues(GetVisibilityAttribute(),
                                          pVisibilityAttributes,
                                          sizeof(uint8_t),
                                          mpMesh->num_points());
        }

        // update visibility attribute
        if (nullptr != pVertexColorAttributes && HasVertexColorInfo())
        {
            UpdateGeometryAttributeValues(GetVertexColorAttribute(),
                                          pVertexColorAttributes,
                                          sizeof(uint8_t) * 3,
                                          mpMesh->num_points());
        }

        // update tex coord attribute
        if (nullptr != pTexCoordAttributes && HasTexCoordInfo())
        {
            UpdateGeometryAttributeValues(GetTexCoordAttribute(),
                                          pTexCoordAttributes,
                                          sizeof(float) * 2,
                                          mpMesh->num_points());
        }

    }

    std::shared_ptr<::draco::Mesh> mpMesh;
    std::shared_ptr<::draco::DecoderBuffer> mpBuffer;
    std::unique_ptr<MeshEdgeBreakerDecompression> mpMeshDecompression;
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
                                unsigned char* pVisibilityAttributes,
                                unsigned char* pVertexColorAttributes,
                                unsigned char* pTexCoordAttributes) const
{
    if (mpImpl->mStatus.ok())
    {
        mpImpl->GetMesh(pVertices,
                        vertexStride,
                        pIndices,
                        pVisibilityAttributes,
                        pVertexColorAttributes,
                        pTexCoordAttributes);
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

bool MeshDecompression::HasVisibilityInfo() const
{
    if (mpImpl->mStatus.ok())
    {
        return mpImpl->HasVisibilityInfo();
    }
    return false;
}

bool MeshDecompression::HasVertexColorInfo() const
{
    if (mpImpl->mStatus.ok())
    {
        return mpImpl->HasVertexColorInfo();
    }
    return false;
}

bool MeshDecompression::hasTexCoordInfo() const
{
    if (mpImpl->mStatus.ok())
    {
        return mpImpl->HasTexCoordInfo();
    }
    return false;
}

const char* MeshDecompression::GetLastErrorMessage() const
{
    return mpImpl->mStatus.error_msg();
}

}; // namespace draco
}; // namespace psy
