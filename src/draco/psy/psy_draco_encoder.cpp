/*
* @file MeshCompression.cpp
*
* Copyright (c) 2016 Personify Inc.
*
* @brief
*   Implements mesh MeshCompression
*/

#include "psy_draco_encoder.h"
#include "draco/compression/config/encoder_options.h"
#include "draco/attributes/point_attribute.h"
#include "draco/compression/mesh/mesh_edgebreaker_encoder.h"

namespace psy
{
static IProfilerManager* gmpProfilerManager = nullptr;
IProfilerManager* GetProfilerManager()
{
    return gmpProfilerManager;
} // GetProfilerManager
void SetProfilerManager(IProfilerManager* pProp)
{
    if (gmpProfilerManager)
    {
        delete gmpProfilerManager;
    }
    gmpProfilerManager = pProp;
} // SetProfilerManager
} // namespace psy

namespace psy
{
namespace draco
{

static const int MAX_COMPRESSION_LEVEL = 10;

class CompressionOptions : public ::draco::DracoOptions<::draco::GeometryAttribute::Type>
{
public:
    CompressionOptions()
    {
        mFeatureOptions.SetBool(::draco::features::kEdgebreaker, true);
        mFeatureOptions.SetBool(::draco::features::kPredictiveEdgebreaker, true);
    }

    ::draco::EncoderOptions CreateEncoderOptions(const ::draco::Mesh& rMesh) const
    {
        auto options = ::draco::EncoderOptions::CreateEmptyOptions();
        options.SetGlobalOptions(this->GetGlobalOptions());
        options.SetFeatureOptions(mFeatureOptions);
        for (int i = 0; i < rMesh.num_attributes(); ++i)
        {
            const auto* p_att_options =
                this->FindAttributeOptions(rMesh.attribute(i)->attribute_type());
            if (p_att_options)
            {
                options.SetAttributeOptions(i, *p_att_options);
            }
        }
        return options;
    }

    // List of supported/unsupported features that can be used by the encoder.
    ::draco::Options mFeatureOptions;
}; // CompressionOptions

class MeshEdgeBreakerCompression : protected ::draco::MeshEdgeBreakerEncoder
{
public:
    MeshEdgeBreakerCompression() : ::draco::MeshEdgeBreakerEncoder()
    {
        mIsIncrementalCompression = false;
    }

    ::draco::Status Compress(const CompressionOptions& rCompressionOptions,
                             const ::draco::Mesh& rMesh,
                             ::draco::EncoderBuffer& rOutBuffer,
                             const bool isIncrementalCompression)
    {
        mIsIncrementalCompression = isIncrementalCompression;
        this->SetMesh(rMesh);
        return Encode(rCompressionOptions.CreateEncoderOptions(rMesh), &rOutBuffer);
    }
protected:
    bool InitializeEncoder() override
    {
        if (mIsIncrementalCompression)
        {
            SetEncoderImplState(*mpEncoderState);
            return true;
        }
        return ::draco::MeshEdgeBreakerEncoder::InitializeEncoder();
    }

    bool EncodeConnectivity() override
    {
        if (mIsIncrementalCompression)
        {
            return true;
        }
        const auto status = ::draco::MeshEdgeBreakerEncoder::EncodeConnectivity();
        if (status)
        {
            mpEncoderState = std::move(CloneEncoderImplState());
        }
        return status;
    }
private:
    bool mIsIncrementalCompression;
    std::unique_ptr<::draco::MeshEdgeBreakerEncoderImplInterface> mpEncoderState;
}; // MeshEdgeBreakerCompression

class MeshCompression::Impl
{
public:
    Impl(int compressionLevel,
         bool hasVisibilityInfo,
         bool hasVertexColorInfo,
         bool hasTexCoordInfo) :
        mCompressionLevel(compressionLevel),
        mHasVisibilityInfo(hasVisibilityInfo),
        mHasVertexColorInfo(hasVertexColorInfo),
        mHasTexCoordInfo(hasTexCoordInfo),
        mPositionAttributeId(0),
        mVertexColorAttributeId(-1),
        mVisibilityAttributeId(-1)
    {
        mCompressionLevel = std::max(0, std::min(MAX_COMPRESSION_LEVEL, mCompressionLevel));

        mpMesh.reset(new ::draco::Mesh());
        mpBuffer.reset(new ::draco::EncoderBuffer());
        mpCompressionOptions.reset(new CompressionOptions());
        mpMeshCompression.reset(new MeshEdgeBreakerCompression());
        {
            int num_attribs = 1;

            ::draco::GeometryAttribute pos_attrib;
            pos_attrib.Init(::draco::GeometryAttribute::POSITION,
                            nullptr, 3, ::draco::DT_INT16, false, sizeof(int16_t) * 3, 0);
            mPositionAttributeId = mpMesh->AddAttribute(pos_attrib, true, 0);

            if (mHasVisibilityInfo)
            {
                ::draco::GeometryAttribute vis_attrib;
                vis_attrib.Init(::draco::GeometryAttribute::GENERIC,
                                nullptr, 1, ::draco::DT_UINT8, false, sizeof(uint8_t), 0);
                mVisibilityAttributeId = mpMesh->AddAttribute(vis_attrib, true, 0);

                num_attribs++;
            }

            if (mHasVertexColorInfo)
            {
                ::draco::GeometryAttribute vertex_color_attrib;
                vertex_color_attrib.Init(::draco::GeometryAttribute::COLOR,
                                         nullptr, 3, ::draco::DT_UINT8, false, sizeof(uint8_t) * 3, 0);
                mVertexColorAttributeId = mpMesh->AddAttribute(vertex_color_attrib, true, 0);
                num_attribs++;
            }

            if (mHasTexCoordInfo)
            {
                ::draco::GeometryAttribute tex_coord_attrib;
                tex_coord_attrib.Init(::draco::GeometryAttribute::TEX_COORD, nullptr, 2, ::draco::DT_FLOAT32, false, sizeof(uint8_t) * 8, 0);
                mTexCoordAttributeId = mpMesh->AddAttribute(tex_coord_attrib, true, 0);
                num_attribs++;
            }

            // Convert compression level to speed (that 0 = slowest, 10 = fastest).
            const int speed = MAX_COMPRESSION_LEVEL - mCompressionLevel;
            mpCompressionOptions->SetGlobalInt("encoding_speed", speed);
            mpCompressionOptions->SetGlobalInt("decoding_speed", speed);
            mpCompressionOptions->SetGlobalBool("split_mesh_on_seams", (num_attribs > 1));
        }
    }

    ~Impl()
    {
        mpMesh.reset();
        mpBuffer.reset();
        mpMeshCompression.reset();
    }

    void ResetGeometryAttributeValues(const size_t verticesCount,
                                      ::draco::PointAttribute* pPointAttribute)
    {
        pPointAttribute->SetIdentityMapping();
        pPointAttribute->Resize(verticesCount);
        pPointAttribute->Reset(verticesCount);
        const auto dst_stride = pPointAttribute->byte_stride();
        uint8_t* p_dst = pPointAttribute->buffer()->data();
        memset(p_dst, 0, verticesCount * dst_stride);
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
        if (stride == static_cast<size_t>(dst_stride))
        {
            memcpy(p_dst, pValues, verticesCount * data_sz_in_bytes);
        }
        else
        {
            for (size_t i = 0; i < verticesCount; ++i, pValues += stride, p_dst += dst_stride)
            {
                memcpy(p_dst, pValues, data_sz_in_bytes);
            }
        }
    } // UpdateGeometryAttributeValues

    MeshCompression::eStatus Run(const int16_t* pVertices,
                                 const size_t vertexStride,
                                 const size_t verticesCount,
                                 const float decodeMultiplier,
                                 const unsigned int* pIndices,
                                 const size_t indicesCount,
                                 const unsigned char* pVisibilityAttributes,
                                 const unsigned char* pVertexColorAttributes,
                                 const unsigned char* pTexCoordAttributes,
                                 const MeshType meshType,
                                 uint32_t IFrameIndex)
    {
        PSY_DRACO_PROFILE_SECTION("MeshCompression::Impl::Run");

        const bool is_incremental_compression = (meshType == MeshType::INCREMENTAL_MESH);

        // reset encode buffer
        mpBuffer->Resize(0);

        // encode header
        /*{
            mHeader.mMajorVersion = PSY_DRACO_API_MAJOR_VERSION;
            mHeader.mMinorVersion = PSY_DRACO_API_MINOR_VERSION;
            mHeader.mDecodeMultiplier = decodeMultiplier;
            mHeader.mMeshType = meshType;
            mHeader.mIFrameIndex = IFrameIndex;

            if (!mpBuffer->Encode(&mHeader, sizeof(mHeader)))
            {
                mStatus = ::draco::Status(::draco::Status::Code::ERROR, "Failed to encode header.");
                return eStatus::FAILED;
            }
        }*/

        // update faces if need
        if (false == is_incremental_compression)
        {
            const size_t faces_count = indicesCount / 3;
            {
                // - we can use SetFace() to update a face at an index,
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

            // update vertex color info
            if (mVertexColorAttributeId >= 0)
            {
                if (pVertexColorAttributes)
                {
                    UpdateGeometryAttributeValues(pVertexColorAttributes,
                                                  sizeof(uint8_t) * 3,
                                                  verticesCount,
                                                  mpMesh->attribute(mVertexColorAttributeId));
                }
                else
                {
                    ResetGeometryAttributeValues(verticesCount,
                                                 mpMesh->attribute(mVertexColorAttributeId));
                }
            }

            // update uv info
            if (mTexCoordAttributeId >= 0)
            {
                assert(nullptr != pTexCoordAttributes);
                {
                    UpdateGeometryAttributeValues(pTexCoordAttributes,
                                                  sizeof(float) * 2,
                                                  verticesCount,
                                                  mpMesh->attribute(mTexCoordAttributeId));
                }
            }
        }

        // run compression
        {
            PSY_DRACO_PROFILE_SECTION("MeshCompression::Impl::Run (EncodeMeshToBuffer)");
            mStatus = mpMeshCompression->Compress(
                          *mpCompressionOptions, *mpMesh, *mpBuffer, is_incremental_compression);
            if (!mStatus.ok())
            {
                return eStatus::FAILED;
            }
        }

        return eStatus::SUCCEED;
    } // MeshCompression::Impl::Run

    int mCompressionLevel;
    bool mHasVisibilityInfo;
    bool mHasVertexColorInfo;
    bool mHasTexCoordInfo;

    int mPositionAttributeId;
    int mVertexColorAttributeId;
    int mVisibilityAttributeId;
    int mTexCoordAttributeId;

    Header mHeader;
    std::unique_ptr<::draco::Mesh> mpMesh;
    std::shared_ptr<::draco::EncoderBuffer> mpBuffer;
    std::unique_ptr<CompressionOptions> mpCompressionOptions;
    std::unique_ptr<MeshEdgeBreakerCompression> mpMeshCompression;
    ::draco::Status mStatus;
}; // MeshCompression::Impl

MeshCompression::MeshCompression(const MeshCompression&) : mpImpl(nullptr) {}
MeshCompression& MeshCompression::operator=(const MeshCompression&)
{
    return *this;
}

MeshCompression::MeshCompression(int compressionLevel,
                                 bool hasVisibilityInfo,
                                 bool hasVertexColorInfo,
                                 bool hasTexCoordInfo)
{
    mpImpl = new Impl(compressionLevel,
                      hasVisibilityInfo,
                      hasVertexColorInfo,
                      hasTexCoordInfo);
}

MeshCompression::~MeshCompression()
{
    if (mpImpl)
    {
        delete mpImpl;
    }
    mpImpl = nullptr;
}

bool MeshCompression::IsVisiblityInfoCompressing() const
{
    return mpImpl->mHasVisibilityInfo;
}

bool MeshCompression::IsVertexColorInfoCompressing() const
{
    return mpImpl->mHasVertexColorInfo;
}

bool MeshCompression::IsTexCoordInfoCompressing() const
{
    return mpImpl->mHasTexCoordInfo;
}

MeshCompression::eStatus MeshCompression::Run(const int16_t* pVertices,
        const size_t vertexStride,
        const size_t verticesCount,
        const float decodeMultiplier,
        const unsigned int* pIndices,
        const size_t indicesCount,
        const unsigned char* pVisibilityAttributes,
        const unsigned char* pVertexColorAttributes,
        const unsigned char* pTexCoordAttributes,
        const MeshType meshType,
        uint32_t IFrameIndex)
{
    return mpImpl->Run(pVertices,
                       vertexStride,
                       verticesCount,
                       decodeMultiplier,
                       pIndices,
                       indicesCount,
                       pVisibilityAttributes,
                       pVertexColorAttributes,
                       pTexCoordAttributes,
                       meshType,
                       IFrameIndex);
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

} // namespace draco
} // namespace psy
