/*
* @file MeshCompression.cpp
*
* Copyright (c) 2016 Personify Inc.
*
* @brief
*   Implements mesh MeshCompression
*/

#include "psy_draco_encoder.h"
#include "draco/compression/encode.h"
#include "draco/attributes/point_attribute.h"

namespace psy
{

static IProfilerManager* gmpProfilerManager = nullptr;
IProfilerManager* GetProfilerManager()
{
    return gmpProfilerManager;
}
void SetProfilerManager(IProfilerManager* pProp)
{
    if (gmpProfilerManager)
    {
        delete gmpProfilerManager;
    }
    gmpProfilerManager = pProp;
}

namespace draco
{

static const int MAX_COMPRESSION_LEVEL = 10;

class MeshCompression::Impl
{
public:
    Impl(int compressionLevel,
         int vertexPositionQuantizationBitsCount,
         bool hasVisibilityInfo) :
        mCompressionLevel(compressionLevel),
        mVertexPositionQuantizationBitsCount(vertexPositionQuantizationBitsCount),
        mHasVisibilityInfo(hasVisibilityInfo),
        mPositionAttributeId(0),
        mVisibilityAttributeId(-1)
    {
        mCompressionLevel = std::max(0, std::min(MAX_COMPRESSION_LEVEL, mCompressionLevel));
        mVertexPositionQuantizationBitsCount = std::max(0, mVertexPositionQuantizationBitsCount);

        mpMesh.reset(new ::draco::Mesh());
        mpBuffer.reset(new ::draco::EncoderBuffer());

        mpEncoder.reset(new ::draco::Encoder());
        {
            mpEncoder->SetAttributeQuantization(
                ::draco::GeometryAttribute::POSITION, mVertexPositionQuantizationBitsCount);
            // Convert compression level to speed (that 0 = slowest, 10 = fastest).
            const int speed = MAX_COMPRESSION_LEVEL - mCompressionLevel;
            mpEncoder->SetSpeedOptions(speed, speed);

            ::draco::GeometryAttribute pos_attrib;
            pos_attrib.Init(::draco::GeometryAttribute::POSITION, nullptr, 3, ::draco::DT_FLOAT32, false, sizeof(float) * 3, 0);
            mPositionAttributeId = mpMesh->AddAttribute(pos_attrib, true, 0);

            if (mHasVisibilityInfo)
            {
                ::draco::GeometryAttribute vis_attrib;
                vis_attrib.Init(::draco::GeometryAttribute::GENERIC, nullptr, 1, ::draco::DT_UINT8, false, sizeof(uint8_t), 0);
                mVisibilityAttributeId = mpMesh->AddAttribute(vis_attrib, true, 0);
            }
        }
    }

    ~Impl()
    {
        mpMesh.reset();
        mpBuffer.reset();
        mpEncoder.reset();
    }

    void UpdateGeometryAttributeValues(const uint8_t* pValues,
                                       const size_t stride,
                                       const size_t verticesCount,
                                       ::draco::PointAttribute* pPointAttribute)
    {
        pPointAttribute->SetIdentityMapping();
        pPointAttribute->Resize(verticesCount);
        pPointAttribute->Reset(verticesCount);
        const size_t data_sz_in_bytes = pPointAttribute->byte_stride();
        const auto dst_stride = pPointAttribute->byte_stride();
        uint8_t* p_dst = pPointAttribute->buffer()->data();
        if (stride == dst_stride)
        {
            memcpy(p_dst, pValues, verticesCount * data_sz_in_bytes);
        }
        else
        {
            for (size_t i = 0, src_idx = 0, dst_idx = 0;
                i < verticesCount; ++i, pValues += stride, p_dst += dst_stride)
            {
                memcpy(p_dst, pValues, data_sz_in_bytes);
            }
        }
    }

    MeshCompression::eStatus Run(const float* pVertices,
                                 const size_t vertexStride,
                                 const size_t verticesCount,
                                 const unsigned int* pIndices,
                                 const size_t indicesCount,
                                 const unsigned char* pVisibilityAttributes)
    {
        PSY_DRACO_PROFILE_SECTION("MeshCompression::Impl::Run");
        // reset encode buffer
        mpBuffer->Resize(0);

        // update faces
        {
            PSY_DRACO_PROFILE_SECTION("MeshCompression::Impl::Run (update faces)");
            const size_t faces_count = indicesCount / 3;
            {
                // - we can using SetFace() to update a face at an index,
                //   however, each call will have a check valid index inside the implementation
                // - instead of, I'm doing an allocation memory first and then push data onto
                mpMesh->SetNumFaces(faces_count); // allocation purpose
                mpMesh->SetNumFaces(0);
            }
            ::draco::Mesh::Face face;
            for (size_t i = 0, j = 0; i < faces_count; i++)
            {
                face[0] = static_cast<int32_t>(pIndices[j++]);
                face[1] = static_cast<int32_t>(pIndices[j++]);
                face[2] = static_cast<int32_t>(pIndices[j++]);
                mpMesh->AddFace(face);
            }
        }

        // update point attributes
        {
            PSY_DRACO_PROFILE_SECTION("MeshCompression::Impl::Run (update attributes)");
            mpMesh->set_num_points(static_cast<int32_t>(verticesCount));

            // vertex positions
            UpdateGeometryAttributeValues(reinterpret_cast<const uint8_t*>(pVertices),
                                          vertexStride,
                                          verticesCount,
                                          mpMesh->attribute(mPositionAttributeId));

            // update visibility info
            if (mVisibilityAttributeId >= 0)
            {
                assert(nullptr != pVisibilityAttributes);
                UpdateGeometryAttributeValues(pVisibilityAttributes,
                                              sizeof(uint8_t),
                                              verticesCount,
                                              mpMesh->attribute(mVisibilityAttributeId));
            }
        }

        // run compression
        {
            PSY_DRACO_PROFILE_SECTION("MeshCompression::Impl::Run (EncodeMeshToBuffer)");
            mStatus = mpEncoder->EncodeMeshToBuffer(*mpMesh, mpBuffer.get());
            if (!mStatus.ok())
            {
                return eStatus::FAILED;
            }
        }

        return eStatus::SUCCEED;
    }

    int mCompressionLevel;
    int mVertexPositionQuantizationBitsCount;
    bool mHasVisibilityInfo;

    int mPositionAttributeId;
    int mVisibilityAttributeId;

    std::unique_ptr<::draco::Mesh> mpMesh;
    std::unique_ptr<::draco::EncoderBuffer> mpBuffer;
    std::unique_ptr<::draco::Encoder> mpEncoder;
    ::draco::Status mStatus;
};

MeshCompression::MeshCompression(int compressionLevel,
                                 int vertexPositionQuantizationBitsCount,
                                 bool hasVisibilityInfo)
{
    mpImpl = new Impl(compressionLevel, vertexPositionQuantizationBitsCount, hasVisibilityInfo);
}

MeshCompression::~MeshCompression()
{
    if (mpImpl)
    {
        delete mpImpl;
    }
    mpImpl = nullptr;
}

MeshCompression::eStatus MeshCompression::Run(const float* pVertices,
                                              const size_t vertexStride,
                                              const size_t verticesCount,
                                              const unsigned int* pIndices,
                                              const size_t indicesCount,
                                              const unsigned char* pVisibilityAttributes)
{
    return mpImpl->Run(pVertices,
                       vertexStride,
                       verticesCount,
                       pIndices,
                       indicesCount,
                       pVisibilityAttributes);
}

const char* MeshCompression::GetCompressedData() const
{
    if (mpImpl->mStatus.ok())
    {
        return mpImpl->mpBuffer->data();
    }
    return nullptr;
}

size_t MeshCompression::GetCompressedDataSizeInBytes() const
{
    if (mpImpl->mStatus.ok())
    {
        return mpImpl->mpBuffer->size();
    }
    return 0;
}

const char* MeshCompression::GetLastErrorMessage() const
{
    return mpImpl->mStatus.error_msg();
}

}; // namespace draco
}; // namespace psy
