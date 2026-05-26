#include "utils.h"

namespace Utils
{
	namespace
	{
		using CalculateBoneMatrices_t = void (*)(RE::BSGeometry*);

		REL::Relocation<CalculateBoneMatrices_t> calculateBoneMatrices{ REL::ID{ 795227, 2277105 } };

		void SetAttributeOffsetRaw(std::uint64_t& a_desc, Vertex::Attribute a_attribute, std::uint32_t a_offset)
		{
			if (a_attribute == Vertex::VA_POSITION) {
				return;
			}

			const auto shift = 4 * static_cast<std::uint8_t>(a_attribute) + 2;
			a_desc = (a_desc & ~(0x3CULL << shift)) | ((static_cast<std::uint64_t>(a_offset) & 0x3C) << shift);
		}

		[[nodiscard]] std::uint16_t FloatToHalf(float a_value)
		{
			std::uint32_t bits;
			std::memcpy(std::addressof(bits), std::addressof(a_value), sizeof(bits));

			const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000);
			auto exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF);
			auto mantissa = bits & 0x7FFFFF;

			if (exponent == 0xFF) {
				return static_cast<std::uint16_t>(sign | (mantissa != 0 ? 0x7E00 : 0x7C00));
			}

			exponent = exponent - 127 + 15;
			if (exponent >= 0x1F) {
				return static_cast<std::uint16_t>(sign | 0x7C00);
			}

			if (exponent <= 0) {
				if (exponent < -10) {
					return sign;
				}

				mantissa |= 0x800000;
				const auto shift = static_cast<std::uint32_t>(14 - exponent);
				auto halfMantissa = static_cast<std::uint16_t>(mantissa >> shift);
				if ((mantissa >> (shift - 1)) & 1) {
					++halfMantissa;
				}
				return static_cast<std::uint16_t>(sign | halfMantissa);
			}

			auto half = static_cast<std::uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
			if (mantissa & 0x1000) {
				++half;
			}
			return half;
		}

		[[nodiscard]] float HalfToFloat(std::uint16_t a_value)
		{
			const auto sign = static_cast<std::uint32_t>(a_value & 0x8000) << 16;
			auto exponent = static_cast<std::uint32_t>((a_value >> 10) & 0x1F);
			auto mantissa = static_cast<std::uint32_t>(a_value & 0x03FF);

			std::uint32_t bits = 0;
			if (exponent == 0) {
				if (mantissa == 0) {
					bits = sign;
				} else {
					exponent = 1;
					while ((mantissa & 0x0400) == 0) {
						mantissa <<= 1;
						--exponent;
					}
					mantissa &= 0x03FF;
					bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
				}
			} else if (exponent == 0x1F) {
				bits = sign | 0x7F800000 | (mantissa << 13);
			} else {
				bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
			}

			float result;
			std::memcpy(std::addressof(result), std::addressof(bits), sizeof(result));
			return result;
		}

		[[nodiscard]] float DecodeSnorm8(std::uint8_t a_value)
		{
			return (static_cast<float>(a_value) / 127.5F) - 1.0F;
		}

		[[nodiscard]] float Dot4(const float* a_row, const float* a_vector)
		{
			return (a_row[0] * a_vector[0]) + (a_row[1] * a_vector[1]) + (a_row[2] * a_vector[2]) + (a_row[3] * a_vector[3]);
		}

		[[nodiscard]] float Dot3(const float* a_row, const float* a_vector)
		{
			return (a_row[0] * a_vector[0]) + (a_row[1] * a_vector[1]) + (a_row[2] * a_vector[2]);
		}

		void Normalize3(float* a_vector)
		{
			const auto lengthSquared =
				(a_vector[0] * a_vector[0]) +
				(a_vector[1] * a_vector[1]) +
				(a_vector[2] * a_vector[2]);
			if (lengthSquared <= 0.0F) {
				a_vector[0] = 0.0F;
				a_vector[1] = 0.0F;
				a_vector[2] = 1.0F;
				return;
			}

			const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
			a_vector[0] *= inverseLength;
			a_vector[1] *= inverseLength;
			a_vector[2] *= inverseLength;
		}

		void WorldToProjectionPoint(const RE::NiTransform& a_world, float* a_point)
		{
			a_point[0] -= a_world.translate.x;
			a_point[1] -= a_world.translate.y;
			a_point[2] -= a_world.translate.z;
		}

		void WorldToLocalVector(const RE::NiTransform& a_world, float* a_vector)
		{
			const float x =
				(a_world.rotate.entry[0].x * a_vector[0]) +
				(a_world.rotate.entry[1].x * a_vector[1]) +
				(a_world.rotate.entry[2].x * a_vector[2]);
			const float y =
				(a_world.rotate.entry[0].y * a_vector[0]) +
				(a_world.rotate.entry[1].y * a_vector[1]) +
				(a_world.rotate.entry[2].y * a_vector[2]);
			const float z =
				(a_world.rotate.entry[0].z * a_vector[0]) +
				(a_world.rotate.entry[1].z * a_vector[1]) +
				(a_world.rotate.entry[2].z * a_vector[2]);

			a_vector[0] = x;
			a_vector[1] = y;
			a_vector[2] = z;
		}

		[[nodiscard]] const float* GetBonePalette(RE::BSGeometry* a_geometry)
		{
			auto* skin = a_geometry->skinInstance.get();
			if (!skin) {
				return nullptr;
			}

			return *reinterpret_cast<const float**>(reinterpret_cast<std::byte*>(skin) + 0xA0);
		}
	}

	std::uint32_t GetStride(const VertexDesc a_desc)
	{
		return static_cast<std::uint32_t>(a_desc.desc & 0xF) * 4;
	}

	std::uint32_t GetAttributeOffsetRaw(std::uint64_t a_desc, Vertex::Attribute a_attribute)
	{
		return static_cast<std::uint32_t>((a_desc >> (4 * static_cast<std::uint8_t>(a_attribute) + 2)) & 0x3C);
	}

	VertexDesc MakeCompactDesc(VertexDesc a_desc)
	{
		const auto oldStride = GetStride(a_desc);
		const auto newStride = oldStride - 8;

		auto compactDesc = a_desc.desc;
		compactDesc &= ~(static_cast<std::uint64_t>(Vertex::VF_FULLPREC) << 44);
		compactDesc = (compactDesc & ~0xFULL) | (newStride / 4);

		for (auto attr = static_cast<std::uint8_t>(Vertex::VA_TEXCOORD0);
			 attr < static_cast<std::uint8_t>(Vertex::VA_COUNT);
			 ++attr) {
			const auto attribute = static_cast<Vertex::Attribute>(attr);
			const auto oldOffset = GetAttributeOffsetRaw(a_desc.desc, attribute);
			if (oldOffset >= 8) {
				SetAttributeOffsetRaw(compactDesc, attribute, oldOffset - 8);
			}
		}

		return VertexDesc{ compactDesc };
	}

	void RepackFullPrecisionVertices(
		const std::byte* a_source,
		std::byte*      a_destination,
		std::uint32_t   a_vertexCount,
		std::uint32_t   a_oldStride,
		std::uint32_t   a_newStride)
	{
		for (std::uint32_t i = 0; i < a_vertexCount; ++i) {
			const auto* source = a_source + (static_cast<std::size_t>(i) * a_oldStride);
			auto* destination = a_destination + (static_cast<std::size_t>(i) * a_newStride);

			std::memset(destination, 0, a_newStride);

			auto* halfPosition = reinterpret_cast<std::uint16_t*>(destination);
			halfPosition[0] = FloatToHalf(*reinterpret_cast<const float*>(source + 0));
			halfPosition[1] = FloatToHalf(*reinterpret_cast<const float*>(source + 4));
			halfPosition[2] = FloatToHalf(*reinterpret_cast<const float*>(source + 8));
			halfPosition[3] = FloatToHalf(*reinterpret_cast<const float*>(source + 12));

			std::memcpy(destination + 8, source + 16, a_oldStride - 16);
		}
	}

	std::uint16_t GetVertexCount(RE::BSGeometry* a_geometry)
	{
		return *reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::byte*>(a_geometry) + 0x164);
	}

	const std::byte* GetVertexData(TriShapeDataAccess* a_dataAccess)
	{
		if (!a_dataAccess) {
			return nullptr;
		}

		const auto* fields = reinterpret_cast<const std::uintptr_t*>(a_dataAccess);
		return reinterpret_cast<const std::byte*>(fields[5]);
	}

	void WritePackedNormal(const std::byte* a_source, std::uint32_t a_normalOffset, void* a_destination)
	{
		auto* normal = static_cast<float*>(a_destination);
		if (a_normalOffset == 0) {
			normal[0] = 0.0F;
			normal[1] = 0.0F;
			normal[2] = 1.0F;
			normal[3] = 0.0F;
			return;
		}

		const auto packed = reinterpret_cast<const std::uint8_t*>(a_source + a_normalOffset);
		normal[0] = DecodeSnorm8(packed[0]);
		normal[1] = DecodeSnorm8(packed[1]);
		normal[2] = DecodeSnorm8(packed[2]);
		normal[3] = DecodeSnorm8(packed[3]);
	}

	void ApplyFullPrecisionSkinning(
		RE::BSGeometry*     a_geometry,
		void*               a_positions,
		void*               a_normals,
		const std::byte*    a_vertexData,
		std::uint32_t       a_vertexCount,
		std::uint32_t       a_stride,
		std::uint32_t       a_normalOffset,
		std::uint32_t       a_skinOffset)
	{
		calculateBoneMatrices(a_geometry);
		const auto* palette = GetBonePalette(a_geometry);
		if (!palette) {
			return;
		}

		for (std::uint32_t i = 0; i < a_vertexCount; ++i) {
			const auto* vertex = a_vertexData + (static_cast<std::size_t>(i) * a_stride);
			const auto* skin = vertex + a_skinOffset;

			float position[4]{
				*reinterpret_cast<const float*>(vertex + 0),
				*reinterpret_cast<const float*>(vertex + 4),
				*reinterpret_cast<const float*>(vertex + 8),
				1.0F
			};
			float normal[4]{ 0.0F, 0.0F, 1.0F, 0.0F };
			if (a_normalOffset != 0) {
				const auto* packed = reinterpret_cast<const std::uint8_t*>(vertex + a_normalOffset);
				normal[0] = DecodeSnorm8(packed[0]);
				normal[1] = DecodeSnorm8(packed[1]);
				normal[2] = DecodeSnorm8(packed[2]);
				normal[3] = 0.0F;
			}

			float weights[4]{
				HalfToFloat(*reinterpret_cast<const std::uint16_t*>(skin + 0)),
				HalfToFloat(*reinterpret_cast<const std::uint16_t*>(skin + 2)),
				HalfToFloat(*reinterpret_cast<const std::uint16_t*>(skin + 4)),
				0.0F
			};
			weights[3] = 1.0F - weights[0] - weights[1] - weights[2];

			const auto* indices = reinterpret_cast<const std::uint8_t*>(skin + 8);
			float skinnedPosition[4]{ 0.0F, 0.0F, 0.0F, 0.0F };
			float skinnedNormal[4]{ 0.0F, 0.0F, 0.0F, 0.0F };

			for (std::uint32_t influence = 0; influence < 4; ++influence) {
				const auto weight = weights[influence];
				if (weight <= 0.0F) {
					continue;
				}

				const auto* matrix = palette + (static_cast<std::size_t>(indices[influence]) * 12);
				const auto* row0 = matrix + 0;
				const auto* row1 = matrix + 4;
				const auto* row2 = matrix + 8;

				skinnedPosition[0] += weight * Dot4(row0, position);
				skinnedPosition[1] += weight * Dot4(row1, position);
				skinnedPosition[2] += weight * Dot4(row2, position);
				skinnedNormal[0] += weight * Dot3(row0, normal);
				skinnedNormal[1] += weight * Dot3(row1, normal);
				skinnedNormal[2] += weight * Dot3(row2, normal);
			}

			WorldToProjectionPoint(a_geometry->world, skinnedPosition);
			WorldToLocalVector(a_geometry->world, skinnedNormal);

			auto* outPosition = static_cast<float*>(a_positions) + (static_cast<std::size_t>(i) * 4);
			outPosition[0] = skinnedPosition[0];
			outPosition[1] = skinnedPosition[1];
			outPosition[2] = skinnedPosition[2];
			outPosition[3] = 0.0F;

			if (a_normals) {
				Normalize3(skinnedNormal);
				auto* outNormal = static_cast<float*>(a_normals) + (static_cast<std::size_t>(i) * 4);
				outNormal[0] = skinnedNormal[0];
				outNormal[1] = skinnedNormal[1];
				outNormal[2] = skinnedNormal[2];
				outNormal[3] = 0.0F;
			}
		}
	}
}
