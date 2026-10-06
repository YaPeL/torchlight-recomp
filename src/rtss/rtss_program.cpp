#include "rtss/rtss_program.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace torchlight::rtss {

namespace {

std::string Trim(std::string_view s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(uint8_t(s[b]))) ++b;
  while (e > b && std::isspace(uint8_t(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

bool IsIdent(char c) { return std::isalnum(uint8_t(c)) || c == '_'; }

// Whether `name` appears in `text` as a whole identifier.
bool ContainsIdent(std::string_view text, std::string_view name) {
  for (size_t p = text.find(name); p != std::string_view::npos; p = text.find(name, p + 1)) {
    bool left = p == 0 || !IsIdent(text[p - 1]);
    bool right = p + name.size() >= text.size() || !IsIdent(text[p + name.size()]);
    if (left && right) return true;
  }
  return false;
}

// Rows of a declared type as constant registers (row-major packing), 0 for non-constants.
uint32_t DeclaredRows(const std::string& type) {
  static const std::map<std::string, uint32_t> kRows = {
      {"float", 1},    {"float2", 1},   {"float3", 1},   {"float4", 1},   {"int", 1},
      {"int2", 1},     {"int3", 1},     {"int4", 1},     {"bool", 1},     {"float2x2", 2},
      {"float2x3", 2}, {"float2x4", 2}, {"float3x2", 3}, {"float3x3", 3}, {"float3x4", 3},
      {"float4x2", 4}, {"float4x3", 4}, {"float4x4", 4}};
  auto it = kRows.find(type);
  return it == kRows.end() ? 0 : it->second;
}

std::vector<std::string> SplitArgs(std::string_view s) {
  std::vector<std::string> out;
  int depth = 0;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '(' || c == '[') ++depth;
    if (c == ')' || c == ']') --depth;
    if (c == ',' && depth == 0) {
      out.push_back(Trim(s.substr(start, i - start)));
      start = i + 1;
    }
  }
  std::string last = Trim(s.substr(start));
  if (!last.empty()) out.push_back(last);
  return out;
}

const Parameter* FindParameter(const Program& p, bool output, std::string_view semantic_prefix) {
  for (const auto& param : p.parameters) {
    if (param.output == output && param.semantic.rfind(semantic_prefix, 0) == 0) return &param;
  }
  return nullptr;
}

}  // namespace

std::string BaseName(std::string_view arg) {
  size_t end = 0;
  while (end < arg.size() && IsIdent(arg[end])) ++end;
  return std::string(arg.substr(0, end));
}

std::optional<Program> Parse(std::string_view source) {
  size_t globals_at = source.find("GLOBAL PARAMETERS");
  size_t main_at = source.find("void main");
  if (globals_at == std::string_view::npos || main_at == std::string_view::npos) return std::nullopt;
  size_t open = source.find('(', main_at);
  size_t close = source.find(')', open);
  size_t body_open = source.find('{', close);
  size_t body_close = source.rfind('}');
  if (open == std::string_view::npos || close == std::string_view::npos ||
      body_open == std::string_view::npos || body_close == std::string_view::npos ||
      body_close < body_open) {
    return std::nullopt;
  }
  std::string_view body = source.substr(body_open + 1, body_close - body_open - 1);

  Program p;
  // Globals: "type name;" or "type name[N];" lines between the section header and main.
  std::string_view decls = source.substr(globals_at, main_at - globals_at);
  size_t line_start = 0;
  while (line_start < decls.size()) {
    size_t line_end = decls.find('\n', line_start);
    if (line_end == std::string_view::npos) line_end = decls.size();
    std::string line = Trim(decls.substr(line_start, line_end - line_start));
    line_start = line_end + 1;
    if (line.empty() || line.rfind("//", 0) == 0 || line.back() != ';') continue;
    line.pop_back();
    size_t colon = line.find(':');  // ": register(sN)" on samplers
    if (colon != std::string::npos) line = Trim(line.substr(0, colon));
    size_t space = line.find_first_of(" \t");
    if (space == std::string::npos) continue;
    Global g;
    g.type = line.substr(0, space);
    std::string rest = Trim(line.substr(space));
    size_t bracket = rest.find('[');
    if (bracket != std::string::npos) {
      g.array = uint32_t(std::stoul(rest.substr(bracket + 1)));
      rest = rest.substr(0, bracket);
    }
    g.name = Trim(rest);
    if (DeclaredRows(g.type) == 0) continue;  // samplers
    if (ContainsIdent(body, g.name)) p.globals.push_back(std::move(g));
  }
  std::sort(p.globals.begin(), p.globals.end(),
            [](const Global& a, const Global& b) { return a.name < b.name; });

  // Parameters: "in|out type name : SEMANTIC".
  for (const std::string& item : SplitArgs(source.substr(open + 1, close - open - 1))) {
    Parameter param;
    size_t colon = item.find(':');
    if (colon == std::string::npos) continue;
    param.semantic = Trim(item.substr(colon + 1));
    std::string decl = Trim(item.substr(0, colon));
    size_t a = decl.find_first_of(" \t");
    if (a == std::string::npos) continue;
    std::string direction = decl.substr(0, a);
    param.output = direction == "out";
    std::string rest = Trim(decl.substr(a));
    size_t b = rest.find_first_of(" \t");
    if (b == std::string::npos) continue;
    param.type = rest.substr(0, b);
    param.name = Trim(rest.substr(b));
    p.parameters.push_back(std::move(param));
  }

  // Calls: "Function(args);" statements.
  size_t pos = 0;
  while (pos < body.size()) {
    size_t semi = body.find(';', pos);
    if (semi == std::string_view::npos) break;
    std::string stmt = Trim(body.substr(pos, semi - pos));
    pos = semi + 1;
    size_t paren = stmt.find('(');
    if (paren == std::string::npos || stmt.back() != ')') continue;
    std::string function = Trim(stmt.substr(0, paren));
    if (function.empty() || !std::all_of(function.begin(), function.end(), IsIdent)) continue;
    p.calls.push_back({function, SplitArgs(std::string_view(stmt).substr(
                                     paren + 1, stmt.size() - paren - 2))});
  }
  return p;
}

bool ResolveRegisters(Program& program, std::span<const AutoPlacement> autos, std::string* error) {
  std::map<uint32_t, uint32_t> auto_by_register;  // first register -> element count
  for (const auto& a : autos) auto_by_register[a.physical_index / 4] = a.element_count;
  uint32_t next = 0;
  for (Global& g : program.globals) {
    g.first_register = next;
    auto it = auto_by_register.find(next);
    if (it != auto_by_register.end() && g.array == 1) {
      g.registers = (it->second + 3) / 4;
    } else {
      g.registers = DeclaredRows(g.type) * g.array;
    }
    next += g.registers;
  }
  for (const auto& [reg, count] : auto_by_register) {
    bool found = std::any_of(program.globals.begin(), program.globals.end(),
                             [&](const Global& g) { return g.first_register == reg; });
    if (!found) {
      if (error) *error = "auto constant at register " + std::to_string(reg) + " starts no global";
      return false;
    }
  }
  return true;
}

const Global* FindGlobal(const Program& program, std::string_view name) {
  for (const auto& g : program.globals)
    if (g.name == name) return &g;
  return nullptr;
}

VertexFeatures AnalyseVertex(const Program& program) {
  VertexFeatures f;
  const Parameter* out_colour = FindParameter(program, true, "COLOR");
  const Parameter* in_colour = FindParameter(program, false, "COLOR");
  for (const Call& c : program.calls) {
    const std::string& fn = c.function;
    if (fn == "FFP_Assign" && c.args.size() == 2 && out_colour &&
        BaseName(c.args[1]) == out_colour->name) {
      std::string src = BaseName(c.args[0]);
      if (in_colour && src == in_colour->name) {
        f.colour = VertexColour::kVertexInput;
      } else if (FindGlobal(program, src)) {
        f.colour = VertexColour::kSceneColour;
        f.scene_colour = src;
      }
    } else if (fn == "FFP_Light_Directional_Diffuse" && c.args.size() == 6) {
      DirectionalLight l;
      l.direction = BaseName(c.args[2]);
      l.diffuse = BaseName(c.args[3]);
      f.lights.push_back(l);
    } else if (fn.rfind("FFP_Light_", 0) == 0 || fn.rfind("SGX_Light_", 0) == 0) {
      f.unsupported.push_back(fn);
    } else if (fn == "FFP_PixelFog_Depth") {
      f.fog_depth = true;
    } else if (fn.rfind("FFP_VertexFog_", 0) == 0) {
      f.unsupported.push_back(fn);
    }
  }
  // Position: the matrix that writes the clip position, and Runic's skinning.
  const Parameter* in_position = FindParameter(program, false, "POSITION");
  const Parameter* out_position = FindParameter(program, true, "POSITION");
  const Parameter* in_indices = FindParameter(program, false, "BLENDINDICES");
  const Parameter* in_weights = FindParameter(program, false, "BLENDWEIGHT");
  auto lane = [](const std::string& arg, const std::string& base) -> std::optional<uint32_t> {
    // "base[N]" or "array[base[N]]"
    size_t at = arg.find(base + "[");
    if (at == std::string::npos) return std::nullopt;
    return uint32_t(std::stoul(arg.substr(at + base.size() + 1)));
  };
  if (in_position && out_position) {
    std::optional<uint32_t> pending_index;  // index lane of the last bone transform
    std::string temp, accumulator;
    Skinning skin;
    for (const Call& c : program.calls) {
      if (c.function == "FFP_Transform" && c.args.size() == 3) {
        std::string src = BaseName(c.args[1]);
        std::string dst = BaseName(c.args[2]);
        auto index = in_indices ? lane(c.args[0], in_indices->name) : std::nullopt;
        if (index && src == in_position->name) {
          skin.bones = BaseName(c.args[0]);
          pending_index = index;
          temp = dst;
        } else if (dst == out_position->name) {
          if (!accumulator.empty() && src == accumulator) skin.position_matrix = BaseName(c.args[0]);
          else f.position_matrix = BaseName(c.args[0]);
        } else if (dst == in_position->name && !accumulator.empty() && src == accumulator) {
          skin.inverse_world = BaseName(c.args[0]);
        }
      } else if (c.function == "FFP_Modulate" && c.args.size() == 3 && pending_index &&
                 in_weights && BaseName(c.args[0]) == temp) {
        if (auto weight = lane(c.args[1], in_weights->name)) {
          skin.influences.push_back({*pending_index, *weight});
          pending_index.reset();
        }
      } else if ((c.function == "FFP_Assign" || c.function == "FFP_Add") && !temp.empty() &&
                 c.args.size() >= 2 && BaseName(c.args[c.args.size() - 2]) == temp &&
                 accumulator.empty()) {
        accumulator = BaseName(c.args.back());
      }
    }
    if (!skin.influences.empty()) {
      if (skin.position_matrix.empty()) f.unsupported.push_back("skinning without a clip transform");
      else f.skinning = skin;
    }
  }

  // Texture coordinate outputs.
  auto output_semantic = [&](const std::string& arg) -> const Parameter* {
    std::string name = BaseName(arg);
    for (const auto& param : program.parameters)
      if (param.output && param.name == name && param.semantic.rfind("TEXCOORD", 0) == 0)
        return &param;
    return nullptr;
  };
  auto input_set = [&](const std::string& arg) -> std::optional<uint32_t> {
    std::string name = BaseName(arg);
    for (const auto& param : program.parameters)
      if (!param.output && param.name == name && param.semantic.rfind("TEXCOORD", 0) == 0)
        return uint32_t(std::stoul(param.semantic.substr(8)));
    return std::nullopt;
  };
  for (const Call& c : program.calls) {
    if (c.args.empty()) continue;
    const Parameter* out = output_semantic(c.args.back());
    if (!out) continue;
    TexCoordOutput t;
    t.semantic = out->semantic;
    const std::string& fn = c.function;
    if (fn == "FFP_Assign" && c.args.size() == 2 && input_set(c.args[0])) {
      t.source = TexCoordSource::kInput;
      t.input_set = *input_set(c.args[0]);
    } else if (fn == "FFP_TransformTexCoord" && c.args.size() == 3 && input_set(c.args[1])) {
      t.source = TexCoordSource::kTransformed;
      t.matrix = BaseName(c.args[0]);
      t.input_set = *input_set(c.args[1]);
    } else if (fn == "FFP_GenerateTexCoord_Projection" && c.args.size() == 4) {
      t.source = TexCoordSource::kProjective;
      t.world = BaseName(c.args[0]);
      t.projector = BaseName(c.args[1]);
    } else if ((fn == "FFP_GenerateTexCoord_EnvMap_Normal" ||
                fn == "FFP_GenerateTexCoord_EnvMap_Sphere") &&
               (c.args.size() == 4 || c.args.size() == 5)) {
      t.source = fn == "FFP_GenerateTexCoord_EnvMap_Normal" ? TexCoordSource::kEnvMapNormal
                                                             : TexCoordSource::kEnvMapSphere;
      if (c.args.size() == 5) t.matrix = BaseName(c.args[2]);
    } else if (fn == "FFP_GenerateTexCoord_EnvMap_Reflect" &&
               (c.args.size() == 6 || c.args.size() == 7)) {
      t.source = TexCoordSource::kEnvMapReflect;
      t.world = BaseName(c.args[0]);
      if (c.args.size() == 7) t.matrix = BaseName(c.args[3]);
    } else if (fn == "FFP_PixelFog_Depth") {
      continue;  // fog distance, not a texture coordinate
    } else {
      f.unsupported.push_back(fn);
      continue;
    }
    f.texcoords.push_back(t);
  }
  // Vertex colour tracking: FFP_Modulate(iColor.xyz, diffuse.xyz, diffuse.xyz) before the light.
  for (auto& l : f.lights) {
    for (const Call& c : program.calls) {
      if (c.function == "FFP_Modulate" && c.args.size() == 3 && in_colour &&
          BaseName(c.args[0]) == in_colour->name && BaseName(c.args[1]) == l.diffuse &&
          BaseName(c.args[2]) == l.diffuse) {
        l.diffuse_from_vertex = true;
      }
    }
  }
  return f;
}

namespace {

std::optional<Operation> OperationOf(const std::string& fn) {
  if (fn == "FFP_Modulate") return Operation::kModulate;
  if (fn == "FFP_ModulateX2") return Operation::kModulateX2;
  if (fn == "FFP_ModulateX4") return Operation::kModulateX4;
  if (fn == "FFP_Add") return Operation::kAdd;
  if (fn == "FFP_AddSigned") return Operation::kAddSigned;
  if (fn == "FFP_AddSmooth") return Operation::kAddSmooth;
  if (fn == "FFP_Subtract") return Operation::kSubtract;
  return std::nullopt;
}

// Components an argument names: colour (xyz), alpha (w) or both.
void Components(const std::string& arg, bool& colour, bool& alpha) {
  size_t dot = arg.find('.');
  std::string swizzle = dot == std::string::npos ? "" : arg.substr(dot + 1);
  colour = swizzle.empty() || swizzle.find_first_of("xyzrgb") != std::string::npos;
  alpha = swizzle.empty() || swizzle.find_first_of("wa") != std::string::npos;
}

}  // namespace

FragmentFeatures AnalyseFragment(const Program& program) {
  FragmentFeatures f;
  const Parameter* in_colour = FindParameter(program, false, "COLOR");
  const Parameter* out_colour = FindParameter(program, true, "COLOR");
  struct Value {
    Operand operand = Operand::kCurrent;
    std::array<float, 4> constant{};
  };
  std::map<std::string, Value> values;  // locals: constants, texel, source1/source2
  auto value_of = [&](const std::string& arg) -> std::optional<Value> {
    std::string name = BaseName(arg);
    if (in_colour && name == in_colour->name) return Value{Operand::kDiffuse, {}};
    if (out_colour && name == out_colour->name) return Value{Operand::kCurrent, {}};
    auto it = values.find(name);
    if (it != values.end()) return it->second;
    return std::nullopt;
  };
  TextureStage* stage = nullptr;
  for (const Call& c : program.calls) {
    const std::string& fn = c.function;
    if (in_colour) {
      for (const auto& a : c.args) f.uses_colour |= BaseName(a) == in_colour->name;
    }
    if (fn == "FFP_Construct" && !c.args.empty()) {
      // FFPLib_Common overloads: (r, g, b, a), (r, g, b), (r, g), and (r) which fills all four
      // components with r.
      Value v{Operand::kConstant, {}};
      size_t n = c.args.size() - 1;
      for (size_t i = 0; i < n && i < 4; ++i) v.constant[i] = std::stof(c.args[i]);
      if (n == 1) v.constant[1] = v.constant[2] = v.constant[3] = v.constant[0];
      values[BaseName(c.args.back())] = v;
    } else if ((fn == "FFP_SampleTexture" || fn == "FFP_SampleTextureProj") && c.args.size() == 3) {
      std::string sampler = BaseName(c.args[0]);
      std::string coord = BaseName(c.args[1]);
      TextureStage t;
      constexpr std::string_view kPrefix = "gTextureSampler";
      if (sampler.rfind(kPrefix, 0) != 0) {
        f.unsupported.push_back(fn + " on " + sampler);
        continue;
      }
      t.sampler = uint32_t(std::stoul(sampler.substr(kPrefix.size())));
      t.projective = fn == "FFP_SampleTextureProj";
      for (const auto& param : program.parameters)
        if (!param.output && param.name == coord) t.coord_semantic = param.semantic;
      f.stages.push_back(t);
      stage = &f.stages.back();
      values[BaseName(c.args[2])] = Value{Operand::kTexture, {}};
    } else if (fn == "FFP_Assign" && c.args.size() == 2 && out_colour &&
               BaseName(c.args[1]) == out_colour->name) {
      // Select: out = source.
      auto v = value_of(c.args[0]);
      if (!stage) continue;  // base colour before the first stage (see uses_colour)
      if (!v) {
        f.unsupported.push_back("assign of " + c.args[0]);
        continue;
      }
      bool colour = false, alpha = false;
      Components(c.args[1], colour, alpha);
      Combine combine;
      combine.operation = Operation::kSource1;
      combine.source1 = v->operand;
      combine.constant1 = v->constant;
      if (colour) stage->colour = combine;
      if (alpha) stage->alpha = combine;
    } else if (fn == "FFP_Assign" && c.args.size() == 2) {
      auto v = value_of(c.args[0]);
      if (v) values[BaseName(c.args[1])] = *v;
    } else if (auto op = OperationOf(fn); op && c.args.size() == 3 && out_colour &&
                                          BaseName(c.args[2]) == out_colour->name) {
      auto a = value_of(c.args[0]);
      auto b = value_of(c.args[1]);
      if (!a || !b) {
        f.unsupported.push_back(fn + " operands");
        continue;
      }
      bool colour = false, alpha = false;
      Components(c.args[2], colour, alpha);
      // current + constant 0 (the RTSS specular term when there is no specular): no-op.
      bool adds_zero = *op == Operation::kAdd && a->operand == Operand::kCurrent &&
                       b->operand == Operand::kConstant &&
                       b->constant == std::array<float, 4>{};
      if (adds_zero) continue;
      if (!stage) {
        f.unsupported.push_back(fn + " before the first texture stage");
        continue;
      }
      Combine combine;
      combine.operation = *op;
      combine.source1 = a->operand;
      combine.constant1 = a->constant;
      combine.source2 = b->operand;
      combine.constant2 = b->constant;
      if (colour) stage->colour = combine;
      if (alpha) stage->alpha = combine;
    } else if (fn == "FFP_PixelFog_Linear" && c.args.size() == 5) {
      f.fog_linear = true;
      f.fog_params = BaseName(c.args[1]);
      f.fog_colour = BaseName(c.args[2]);
    } else if (fn.rfind("FFP_PixelFog_", 0) == 0) {
      f.unsupported.push_back(fn);
    } else if (fn != "FFP_Assign") {
      f.unsupported.push_back(fn);
    }
  }
  return f;
}

}  // namespace torchlight::rtss
