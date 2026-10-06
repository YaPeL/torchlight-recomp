// Guest OGRE 1.7 enum values -> neutral command enums.
//
// Every translation is an explicit table over the guest_abi constants; a guest value missing from
// a table stays unknown and keeps its raw value. No guest and neutral numbering is assumed equal.

#pragma once

#include <cstdint>
#include <utility>

#include "commands/types.h"
#include "guest_abi/ogre_enums.h"

namespace torchlight::capture {

template <typename E, size_t N>
commands::Enum<E> Translate(uint32_t raw,
                            const std::pair<guest_abi::ogre::EnumValue, E> (&table)[N]) {
  for (const auto& [guest, neutral] : table) {
    if (guest.value == raw) return {static_cast<uint8_t>(neutral), raw};
  }
  return {commands::kUnknown, raw};
}

inline commands::Enum<commands::PrimitiveType> ToPrimitive(uint32_t raw) {
  namespace g = guest_abi::ogre::operation_type;
  using P = commands::PrimitiveType;
  static constexpr std::pair<guest_abi::ogre::EnumValue, P> t[] = {
      {g::kPointList, P::kPointList},       {g::kLineList, P::kLineList},
      {g::kLineStrip, P::kLineStrip},       {g::kTriangleList, P::kTriangleList},
      {g::kTriangleStrip, P::kTriangleStrip}, {g::kTriangleFan, P::kTriangleFan}};
  return Translate(raw, t);
}

inline commands::Enum<commands::BlendFactor> ToBlendFactor(uint32_t raw) {
  namespace g = guest_abi::ogre::scene_blend_factor;
  using B = commands::BlendFactor;
  static constexpr std::pair<guest_abi::ogre::EnumValue, B> t[] = {
      {g::kOne, B::kOne},
      {g::kZero, B::kZero},
      {g::kDestColour, B::kDestColour},
      {g::kSourceColour, B::kSourceColour},
      {g::kOneMinusDestColour, B::kOneMinusDestColour},
      {g::kOneMinusSourceColour, B::kOneMinusSourceColour},
      {g::kDestAlpha, B::kDestAlpha},
      {g::kSourceAlpha, B::kSourceAlpha},
      {g::kOneMinusDestAlpha, B::kOneMinusDestAlpha},
      {g::kOneMinusSourceAlpha, B::kOneMinusSourceAlpha}};
  return Translate(raw, t);
}

inline commands::Enum<commands::BlendOp> ToBlendOp(uint32_t raw) {
  namespace g = guest_abi::ogre::scene_blend_operation;
  using B = commands::BlendOp;
  static constexpr std::pair<guest_abi::ogre::EnumValue, B> t[] = {
      {g::kAdd, B::kAdd}, {g::kSubtract, B::kSubtract}, {g::kReverseSubtract, B::kReverseSubtract},
      {g::kMin, B::kMin}, {g::kMax, B::kMax}};
  return Translate(raw, t);
}

inline commands::Enum<commands::CompareFunc> ToCompare(uint32_t raw) {
  namespace g = guest_abi::ogre::compare_function;
  using C = commands::CompareFunc;
  static constexpr std::pair<guest_abi::ogre::EnumValue, C> t[] = {
      {g::kAlwaysFail, C::kNever},   {g::kAlwaysPass, C::kAlways},
      {g::kLess, C::kLess},          {g::kLessEqual, C::kLessEqual},
      {g::kEqual, C::kEqual},        {g::kNotEqual, C::kNotEqual},
      {g::kGreaterEqual, C::kGreaterEqual}, {g::kGreater, C::kGreater}};
  return Translate(raw, t);
}

inline commands::Enum<commands::CullMode> ToCull(uint32_t raw) {
  namespace g = guest_abi::ogre::culling_mode;
  using C = commands::CullMode;
  static constexpr std::pair<guest_abi::ogre::EnumValue, C> t[] = {
      {g::kNone, C::kNone}, {g::kClockwise, C::kClockwise}, {g::kAnticlockwise, C::kAnticlockwise}};
  return Translate(raw, t);
}

inline commands::Enum<commands::FilterStage> ToFilterStage(uint32_t raw) {
  namespace g = guest_abi::ogre::filter_type;
  using F = commands::FilterStage;
  static constexpr std::pair<guest_abi::ogre::EnumValue, F> t[] = {
      {g::kMin, F::kMin}, {g::kMag, F::kMag}, {g::kMip, F::kMip}};
  return Translate(raw, t);
}

inline commands::Enum<commands::Filter> ToFilter(uint32_t raw) {
  namespace g = guest_abi::ogre::filter_options;
  using F = commands::Filter;
  static constexpr std::pair<guest_abi::ogre::EnumValue, F> t[] = {
      {g::kNone, F::kNone}, {g::kPoint, F::kPoint}, {g::kLinear, F::kLinear},
      {g::kAnisotropic, F::kAnisotropic}};
  return Translate(raw, t);
}

inline commands::Enum<commands::AddressMode> ToAddress(uint32_t raw) {
  namespace g = guest_abi::ogre::texture_addressing_mode;
  using A = commands::AddressMode;
  static constexpr std::pair<guest_abi::ogre::EnumValue, A> t[] = {
      {g::kWrap, A::kWrap}, {g::kMirror, A::kMirror}, {g::kClamp, A::kClamp},
      {g::kBorder, A::kBorder}};
  return Translate(raw, t);
}

inline commands::Enum<commands::PolygonMode> ToPolygonMode(uint32_t raw) {
  namespace g = guest_abi::ogre::polygon_mode;
  using P = commands::PolygonMode;
  static constexpr std::pair<guest_abi::ogre::EnumValue, P> t[] = {
      {g::kPoints, P::kPoints}, {g::kWireframe, P::kWireframe}, {g::kSolid, P::kSolid}};
  return Translate(raw, t);
}

inline commands::Enum<commands::StencilOp> ToStencilOp(uint32_t raw) {
  namespace g = guest_abi::ogre::stencil_operation;
  using S = commands::StencilOp;
  static constexpr std::pair<guest_abi::ogre::EnumValue, S> t[] = {
      {g::kKeep, S::kKeep},         {g::kZero, S::kZero},
      {g::kReplace, S::kReplace},   {g::kIncrement, S::kIncrement},
      {g::kDecrement, S::kDecrement}, {g::kIncrementWrap, S::kIncrementWrap},
      {g::kDecrementWrap, S::kDecrementWrap}, {g::kInvert, S::kInvert}};
  return Translate(raw, t);
}

inline commands::Enum<commands::VertexType> ToVertexType(uint32_t raw) {
  namespace g = guest_abi::ogre::vertex_element_type;
  using V = commands::VertexType;
  static constexpr std::pair<guest_abi::ogre::EnumValue, V> t[] = {
      {g::kFloat1, V::kFloat1}, {g::kFloat2, V::kFloat2}, {g::kFloat3, V::kFloat3},
      {g::kFloat4, V::kFloat4}, {g::kColour, V::kColour}, {g::kShort1, V::kShort1},
      {g::kShort2, V::kShort2}, {g::kShort3, V::kShort3}, {g::kShort4, V::kShort4},
      {g::kUbyte4, V::kUbyte4}, {g::kColourArgb, V::kColourArgb},
      {g::kColourAbgr, V::kColourAbgr}};
  return Translate(raw, t);
}

inline commands::Enum<commands::VertexSemantic> ToVertexSemantic(uint32_t raw) {
  namespace g = guest_abi::ogre::vertex_element_semantic;
  using V = commands::VertexSemantic;
  static constexpr std::pair<guest_abi::ogre::EnumValue, V> t[] = {
      {g::kPosition, V::kPosition},       {g::kBlendWeights, V::kBlendWeights},
      {g::kBlendIndices, V::kBlendIndices}, {g::kNormal, V::kNormal},
      {g::kDiffuse, V::kDiffuse},         {g::kSpecular, V::kSpecular},
      {g::kTextureCoordinates, V::kTextureCoordinates}, {g::kBinormal, V::kBinormal},
      {g::kTangent, V::kTangent}};
  return Translate(raw, t);
}

inline commands::Enum<commands::ProgramStage> ToStage(uint32_t raw) {
  namespace g = guest_abi::ogre::gpu_program_type;
  using P = commands::ProgramStage;
  static constexpr std::pair<guest_abi::ogre::EnumValue, P> t[] = {
      {g::kVertex, P::kVertex}, {g::kFragment, P::kFragment}, {g::kGeometry, P::kGeometry}};
  return Translate(raw, t);
}

inline commands::Enum<commands::TextureType> ToTextureType(uint32_t raw) {
  namespace g = guest_abi::ogre::texture_type;
  using T = commands::TextureType;
  static constexpr std::pair<guest_abi::ogre::EnumValue, T> t[] = {
      {g::k1D, T::k1D}, {g::k2D, T::k2D}, {g::k3D, T::k3D}, {g::kCubeMap, T::kCubeMap}};
  return Translate(raw, t);
}

}  // namespace torchlight::capture
