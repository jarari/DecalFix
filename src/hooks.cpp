#include "hooks.h"

#include "utils.h"

namespace Hooks
{
	namespace
	{
		using VertexDesc = RE::BSGraphics::VertexDesc;
		using Vertex = RE::BSGraphics::Vertex;
		using TriShapeDataAccess = Utils::TriShapeDataAccess;

		using CreateVertexBuffer_t = RE::BSGraphics::VertexBuffer* (*)(
			RE::BSGraphics::Renderer*,
			std::uint32_t*,
			void*,
			std::uint32_t,
			std::uint64_t);
		using CreateTriShape_t = RE::BSGraphics::TriShape* (*)(
			RE::BSGraphics::Renderer*,
			RE::BSGraphics::VertexBuffer*,
			std::uint64_t,
			std::uint16_t*,
			std::uint32_t);
		using BSSubIndexTriShapeCtor_t = RE::BSSubIndexTriShape* (*)(
			void*,
			RE::BSGraphics::TriShape*,
			std::uint64_t,
			std::uint32_t,
			std::uint32_t,
			std::uint32_t);
		using ApplySkinningToGeometry_t = void (*)(
			RE::BSGeometry*,
			void*,
			void*,
			TriShapeDataAccess*);
		using CreatePositionData_t = void* (*)(RE::BSTriShape*);
		using GetTriShapeDataAccess_t = TriShapeDataAccess* (*)(RE::BSTriShape*, bool);
		using CreatePositionDataFromVertexIndexData_t = void* (*)(
			const void*,
			const void*,
			std::uint32_t,
			std::uint32_t,
			const void*,
			bool);

		struct DecalPatchState
		{
			bool active{ false };
			std::uint64_t compactDesc{ 0 };
			RE::BSGraphics::VertexBuffer* vertexBuffer{ nullptr };
			RE::BSGraphics::TriShape* triShape{ nullptr };
			std::vector<std::byte> compactVertices;
		};

		struct EffectShaderPatchState
		{
			bool active{ false };
			RE::BSTriShape* shape{ nullptr };
			std::uint64_t originalDesc{ 0 };
			std::uint64_t compactDesc{ 0 };
			std::vector<std::byte> compactData;
		};

		CreateVertexBuffer_t originalCreateVertexBuffer{ nullptr };
		CreateTriShape_t originalCreateTriShape{ nullptr };
		BSSubIndexTriShapeCtor_t originalBSSubIndexTriShapeCtor{ nullptr };
		ApplySkinningToGeometry_t originalApplySkinningToGeometry{ nullptr };
		CreatePositionData_t originalCreatePositionData{ nullptr };
		GetTriShapeDataAccess_t originalEffectShaderGetTriShapeDataAccess{ nullptr };
		CreatePositionDataFromVertexIndexData_t originalEffectShaderCreatePositionDataFromVertexIndexData{ nullptr };

		thread_local DecalPatchState decalPatchState;
		thread_local EffectShaderPatchState effectShaderPatchState;

		void ApplySkinningToGeometry(
			RE::BSGeometry*     a_geometry,
			void*               a_positions,
			void*               a_normals,
			TriShapeDataAccess* a_dataAccess)
		{
			originalApplySkinningToGeometry(a_geometry, a_positions, a_normals, a_dataAccess);

			if (!a_geometry || !a_positions) {
				return;
			}

			const VertexDesc desc{ a_geometry->vertexDesc.desc };
			if (!desc.HasFlag(Vertex::VF_FULLPREC)) {
				return;
			}

			const auto vertexData = Utils::GetVertexData(a_dataAccess);
			const auto vertexCount = Utils::GetVertexCount(a_geometry);
			const auto stride = Utils::GetStride(desc);
			if (!vertexData || vertexCount == 0 || stride < 16) {
				return;
			}

			const auto normalOffset = Utils::GetAttributeOffsetRaw(desc.desc, Vertex::VA_NORMAL);
			const auto skinOffset = Utils::GetAttributeOffsetRaw(desc.desc, Vertex::VA_SKINNING);
			if (a_geometry->skinInstance && skinOffset != 0) {
				Utils::ApplyFullPrecisionSkinning(
					a_geometry,
					a_positions,
					a_normals,
					vertexData,
					vertexCount,
					stride,
					normalOffset,
					skinOffset);
			} else {
				for (std::uint32_t i = 0; i < vertexCount; ++i) {
					const auto* source = vertexData + (static_cast<std::size_t>(i) * stride);
					auto* position = static_cast<std::byte*>(a_positions) + (static_cast<std::size_t>(i) * 16);
					std::memcpy(position, source, 16);

					if (a_normals) {
						auto* normal = static_cast<std::byte*>(a_normals) + (static_cast<std::size_t>(i) * 16);
						Utils::WritePackedNormal(source, normalOffset, normal);
					}
				}
			}
		}

		RE::BSGraphics::VertexBuffer* CreateVertexBuffer(
			RE::BSGraphics::Renderer* a_renderer,
			std::uint32_t*            a_size,
			void*                     a_data,
			std::uint32_t             a_stride,
			std::uint64_t             a_desc)
		{
			decalPatchState = {};

			VertexDesc oldDesc{ a_desc };
			if (a_data && a_size && a_stride >= 24 && oldDesc.HasFlag(Vertex::VF_FULLPREC)) {
				const auto vertexCount = *a_size / a_stride;
				if (vertexCount > 0) {
					const auto compactDesc = Utils::MakeCompactDesc(oldDesc);
					const auto compactStride = Utils::GetStride(compactDesc);
					const auto compactSize = vertexCount * compactStride;

					decalPatchState.compactVertices.resize(compactSize);
					Utils::RepackFullPrecisionVertices(
						static_cast<const std::byte*>(a_data),
						decalPatchState.compactVertices.data(),
						vertexCount,
						a_stride,
						compactStride);

					auto uploadSize = compactSize;
					auto* vertexBuffer = originalCreateVertexBuffer(
						a_renderer,
						std::addressof(uploadSize),
						decalPatchState.compactVertices.data(),
						compactStride,
						compactDesc.desc);

					decalPatchState.active = true;
					decalPatchState.compactDesc = compactDesc.desc;
					decalPatchState.vertexBuffer = vertexBuffer;

					return vertexBuffer;
				}
			}

			return originalCreateVertexBuffer(a_renderer, a_size, a_data, a_stride, a_desc);
		}

		RE::BSGraphics::TriShape* CreateTriShape(
			RE::BSGraphics::Renderer*     a_renderer,
			RE::BSGraphics::VertexBuffer* a_vertexBuffer,
			std::uint64_t                 a_desc,
			std::uint16_t*                a_indices,
			std::uint32_t                 a_indexCount)
		{
			auto desc = a_desc;
			if (decalPatchState.active && decalPatchState.vertexBuffer == a_vertexBuffer) {
				desc = decalPatchState.compactDesc;
			}

			auto* triShape = originalCreateTriShape(a_renderer, a_vertexBuffer, desc, a_indices, a_indexCount);
			if (decalPatchState.active && decalPatchState.vertexBuffer == a_vertexBuffer) {
				decalPatchState.triShape = triShape;
			}

			return triShape;
		}

		RE::BSSubIndexTriShape* BSSubIndexTriShapeCtor(
			void*                     a_this,
			RE::BSGraphics::TriShape* a_triShape,
			std::uint64_t             a_desc,
			std::uint32_t             a_vertexCount,
			std::uint32_t             a_primitiveCount,
			std::uint32_t             a_segmentCount)
		{
			auto desc = a_desc;
			if (decalPatchState.active && decalPatchState.triShape == a_triShape) {
				desc = decalPatchState.compactDesc;
			}

			auto* result = originalBSSubIndexTriShapeCtor(
				a_this,
				a_triShape,
				desc,
				a_vertexCount,
				a_primitiveCount,
				a_segmentCount);

			if (decalPatchState.active && decalPatchState.triShape == a_triShape) {
				decalPatchState = {};
			}

			return result;
		}

		TriShapeDataAccess* EffectShaderGetTriShapeDataAccess(RE::BSTriShape* a_shape, bool a_arg2)
		{
			if (!effectShaderPatchState.active && a_shape) {
				const VertexDesc originalDesc{ a_shape->vertexDesc.desc };
				if (originalDesc.HasFlag(Vertex::VF_FULLPREC)) {
					const auto compactDesc = Utils::MakeCompactDesc(originalDesc);
					effectShaderPatchState = {};
					effectShaderPatchState.active = true;
					effectShaderPatchState.shape = a_shape;
					effectShaderPatchState.originalDesc = originalDesc.desc;
					effectShaderPatchState.compactDesc = compactDesc.desc;
				}
			}

			if (effectShaderPatchState.active && effectShaderPatchState.shape == a_shape) {
				const VertexDesc compactDesc{ effectShaderPatchState.compactDesc };
				const VertexDesc originalDesc{ effectShaderPatchState.originalDesc };

				a_shape->vertexDesc = originalDesc;
				auto* result = originalEffectShaderGetTriShapeDataAccess(a_shape, a_arg2);
				a_shape->vertexDesc = compactDesc;

				if (result) {
					const auto* vertexData = Utils::GetVertexData(result);
					const auto vertexCount = a_shape->numVertices;
					const auto indexCount = static_cast<std::uint32_t>(a_shape->numTriangles) * 3;
					const auto originalStride = Utils::GetStride(originalDesc);
					const auto compactStride = Utils::GetStride(compactDesc);

					if (vertexData && vertexCount > 0 && originalStride > compactStride && compactStride >= 8) {
						auto* fields = reinterpret_cast<std::uintptr_t*>(result);
						const auto compactVertexBytes = static_cast<std::uint32_t>(vertexCount) * compactStride;
						const auto indexBytes = indexCount * sizeof(std::uint16_t);
						const auto* originalIndexData =
							fields[0] != 0 ?
								vertexData + static_cast<std::uint32_t>(fields[4]) :
								reinterpret_cast<const std::byte*>(fields[6]);

						effectShaderPatchState.compactData.resize(static_cast<std::size_t>(compactVertexBytes) + indexBytes);
						Utils::RepackFullPrecisionVertices(
							vertexData,
							effectShaderPatchState.compactData.data(),
							vertexCount,
							originalStride,
							compactStride);
						if (originalIndexData && indexBytes > 0) {
							std::memcpy(effectShaderPatchState.compactData.data() + compactVertexBytes, originalIndexData, indexBytes);
						}

						fields[5] = reinterpret_cast<std::uintptr_t>(effectShaderPatchState.compactData.data());
						if (fields[0] != 0) {
							fields[4] = compactVertexBytes;
						} else {
							fields[6] = reinterpret_cast<std::uintptr_t>(effectShaderPatchState.compactData.data() + compactVertexBytes);
						}
					}
				}

				return result;
			}

			return originalEffectShaderGetTriShapeDataAccess(a_shape, a_arg2);
		}

		void* EffectShaderCreatePositionDataFromVertexIndexData(
			const void*   a_vertices,
			const void*   a_indices,
			std::uint32_t a_vertexCount,
			std::uint32_t a_indexCount,
			const void*   a_offsets,
			bool          a_hasSkinning)
		{
			auto* result = originalEffectShaderCreatePositionDataFromVertexIndexData(
				a_vertices,
				a_indices,
				a_vertexCount,
				a_indexCount,
				a_offsets,
				a_hasSkinning);

			if (effectShaderPatchState.active && effectShaderPatchState.shape) {
				effectShaderPatchState.shape->vertexDesc = VertexDesc{ effectShaderPatchState.originalDesc };
				effectShaderPatchState = {};
			}

			return result;
		}

		void* CreatePositionData(RE::BSTriShape* a_shape)
		{
			if (!a_shape) {
				return originalCreatePositionData(a_shape);
			}

			const VertexDesc originalDesc{ a_shape->vertexDesc.desc };
			if (!originalDesc.HasFlag(Vertex::VF_FULLPREC)) {
				return originalCreatePositionData(a_shape);
			}

			const auto compactDesc = Utils::MakeCompactDesc(originalDesc);
			effectShaderPatchState = {};
			effectShaderPatchState.active = true;
			effectShaderPatchState.shape = a_shape;
			effectShaderPatchState.originalDesc = originalDesc.desc;
			effectShaderPatchState.compactDesc = compactDesc.desc;

			a_shape->vertexDesc = compactDesc;
			auto* result = originalCreatePositionData(a_shape);
			a_shape->vertexDesc = originalDesc;
			effectShaderPatchState = {};
			return result;
		}

		void InstallDecalProjectionHooks()
		{
			const auto isOG = REX::FModule::IsRuntimeOG();
			REL::Relocation<std::uintptr_t> decalProjection{ REL::ID{ 825090, 2212077 } };
			if (isOG) {
				originalApplySkinningToGeometry = reinterpret_cast<ApplySkinningToGeometry_t>(
					decalProjection.write_call<5, 0x136>(ApplySkinningToGeometry));
				originalCreateVertexBuffer = reinterpret_cast<CreateVertexBuffer_t>(
					decalProjection.write_call<5, 0x6D8>(CreateVertexBuffer));
				originalCreateTriShape = reinterpret_cast<CreateTriShape_t>(
					decalProjection.write_call<5, 0x6F9>(CreateTriShape));
				originalBSSubIndexTriShapeCtor = reinterpret_cast<BSSubIndexTriShapeCtor_t>(
					decalProjection.write_call<5, 0x78C>(BSSubIndexTriShapeCtor));
			} else {
				originalApplySkinningToGeometry = reinterpret_cast<ApplySkinningToGeometry_t>(
					decalProjection.write_call<5, 0x135>(ApplySkinningToGeometry));
				originalCreateVertexBuffer = reinterpret_cast<CreateVertexBuffer_t>(
					decalProjection.write_call<5, 0x67C>(CreateVertexBuffer));
				originalCreateTriShape = reinterpret_cast<CreateTriShape_t>(
					decalProjection.write_call<5, 0x6A6>(CreateTriShape));
				originalBSSubIndexTriShapeCtor = reinterpret_cast<BSSubIndexTriShapeCtor_t>(
					decalProjection.write_call<5, 0x742>(BSSubIndexTriShapeCtor));
			}
		}

		void InstallEffectShaderParticleHooks()
		{
			if (REX::FModule::IsRuntimeOG()) {
				REL::Relocation<std::uintptr_t> createPositionData{ REL::ID{ 1037836, 0 } };
				originalEffectShaderGetTriShapeDataAccess =
					reinterpret_cast<GetTriShapeDataAccess_t>(
						createPositionData.write_call<5, 0x37>(EffectShaderGetTriShapeDataAccess));

				REL::Relocation<std::uintptr_t> setupMSTarget{ REL::ID{ 146786, 2194489 } };
				originalCreatePositionData = reinterpret_cast<CreatePositionData_t>(
					setupMSTarget.write_call<5, 0xA5>(CreatePositionData));
			} else {
				REL::Relocation<std::uintptr_t> setupMSTarget{ REL::ID{ 146786, 2194489 } };
				originalEffectShaderGetTriShapeDataAccess =
					reinterpret_cast<GetTriShapeDataAccess_t>(
						setupMSTarget.write_call<5, 0xD0>(EffectShaderGetTriShapeDataAccess));
				originalEffectShaderCreatePositionDataFromVertexIndexData =
					reinterpret_cast<CreatePositionDataFromVertexIndexData_t>(
						setupMSTarget.write_call<5, 0x155>(EffectShaderCreatePositionDataFromVertexIndexData));
			}
		}
	}

	void Install()
	{
		InstallDecalProjectionHooks();
		InstallEffectShaderParticleHooks();
	}
}
