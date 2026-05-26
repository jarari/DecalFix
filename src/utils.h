#pragma once

namespace Utils
{
	using VertexDesc = RE::BSGraphics::VertexDesc;
	using Vertex = RE::BSGraphics::Vertex;
	using TriShapeDataAccess = void;

	[[nodiscard]] std::uint32_t GetStride(VertexDesc a_desc);
	[[nodiscard]] std::uint32_t GetAttributeOffsetRaw(std::uint64_t a_desc, Vertex::Attribute a_attribute);
	[[nodiscard]] VertexDesc MakeCompactDesc(VertexDesc a_desc);
	void RepackFullPrecisionVertices(
		const std::byte* a_source,
		std::byte*      a_destination,
		std::uint32_t   a_vertexCount,
		std::uint32_t   a_oldStride,
		std::uint32_t   a_newStride);

	[[nodiscard]] std::uint16_t GetVertexCount(RE::BSGeometry* a_geometry);
	[[nodiscard]] const std::byte* GetVertexData(TriShapeDataAccess* a_dataAccess);
	void WritePackedNormal(const std::byte* a_source, std::uint32_t a_normalOffset, void* a_destination);
	void ApplyFullPrecisionSkinning(
		RE::BSGeometry*     a_geometry,
		void*               a_positions,
		void*               a_normals,
		const std::byte*    a_vertexData,
		std::uint32_t       a_vertexCount,
		std::uint32_t       a_stride,
		std::uint32_t       a_normalOffset,
		std::uint32_t       a_skinOffset);
}
