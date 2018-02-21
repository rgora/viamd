#include "molecule_utils.h"
#include <gfx/gl_utils.h>
#include <core/common.h>
#include <core/math_utils.h>
#include <core/hash.h>
#include <imgui.h>

void transform_positions(Array<vec3> positions, const mat4& transformation) {
    for (auto& p : positions) {
        p = vec3(transformation * vec4(p, 1));
    }
}

void compute_bounding_box(vec3* min_box, vec3* max_box, const Array<vec3> positions) {
    ASSERT(min_box);
    ASSERT(max_box);

    if (positions.count == 0) {
        *min_box = *max_box = vec3(0);
    }

    *min_box = *max_box = positions.data[0];
    for (int64 i = 0; i < positions.count; i++) {
        *min_box = math::min(*min_box, positions.data[i]);
        *max_box = math::max(*max_box, positions.data[i]);
    }
}

// @TODO: Fix this, is it possible in theory to get a good interpolation between frames with periodicity without modifying source data?
void linear_interpolation_periodic(Array<vec3> positions, const Array<vec3> prev_pos, const Array<vec3> next_pos, float t, mat3 sim_box) {
    ASSERT(prev_pos.count == positions.count);
    ASSERT(next_pos.count == positions.count);

    vec3 full_box_ext = sim_box * vec3(1);
    vec3 half_box_ext = full_box_ext * 0.5f;

    for (int i = 0; i < positions.count; i++) {
        vec3 next = next_pos[i];
        vec3 prev = prev_pos[i];

        vec3 delta = next - prev;
        vec3 sign = math::sign(delta);

        vec3 abs_delta = math::abs(delta);
        if (abs_delta.x > half_box_ext.x) next.x -= full_box_ext.x * sign.x;
        if (abs_delta.y > half_box_ext.y) next.y -= full_box_ext.y * sign.y;
        if (abs_delta.z > half_box_ext.z) next.z -= full_box_ext.z * sign.z;

        // next -= math::step(half_box_ext, math::abs(delta)) * delta;

        positions[i] = math::mix(prev, next, t);
    }
}

void linear_interpolation(Array<vec3> positions, const Array<vec3> prev_pos, const Array<vec3> next_pos, float t) {
	ASSERT(prev_pos.count == positions.count);
	ASSERT(next_pos.count == positions.count);

	for (int i = 0; i < positions.count; i++) {
		positions[i] = math::mix(prev_pos[i], next_pos[i], t);
	}
}

DynamicArray<Bond> compute_atomic_bonds(const Array<vec3> atom_pos, const Array<Element> atom_elem, const Array<Residue> residues, Allocator* alloc) {
    ASSERT(atom_pos.count == atom_elem.count);

    DynamicArray<Bond> bonds;

    auto try_and_create_atom_bond = [& pos = atom_pos, &elem = atom_elem, &bonds = bonds](int i, int j) -> bool {
        auto d = element::covalent_radius(elem[i]) + element::covalent_radius(elem[j]);
        auto d1 = d + 0.3f;
        auto d2 = d - 0.5f;
        auto v = pos[i] - pos[j];
        auto dist2 = glm::dot(v, v);

        if (dist2 < (d1 * d1) && dist2 > (d2 * d2)) {
            bonds.push_back({i, j});
            return true;
        } else
            return false;
    };

    if (residues) {
        // Create connections within residues
        for (int64 i = 0; i < residues.count; i++) {
            const Residue& res = residues[i];
            for (int atom_i = res.beg_atom_idx; atom_i < res.end_atom_idx - 1; atom_i++) {
                for (int atom_j = atom_i + 1; atom_j < res.end_atom_idx; atom_j++) {
                    try_and_create_atom_bond(atom_i, atom_j);
                }
            }
        }

        // @TODO:
        // Create connections between consecutive residues (Is it enough and correct to only check
        // consecutive residues?)

        for (int res_i = 0; res_i < residues.count - 1; res_i++) {
            auto res_j = res_i + 1;
            auto atom_i_beg = residues[res_i].beg_atom_idx;
            auto atom_i_end = residues[res_i].end_atom_idx;
            auto atom_j_beg = residues[res_j].beg_atom_idx;
            auto atom_j_end = residues[res_j].end_atom_idx;

            for (int atom_i = atom_i_beg; atom_i < atom_i_end; atom_i++) {
                for (int atom_j = atom_j_beg; atom_j < atom_j_end; atom_j++) {
                    try_and_create_atom_bond(atom_i, atom_j);
                }
            }
        }
    } else {
        // Brute force N^2 check
        // @TODO: Use spatial hash
        auto atom_count = atom_pos.count;
        for (int atom_i = 0; atom_i < atom_count - 1; ++atom_i) {
            for (int atom_j = 1; atom_j < atom_count; ++atom_j) {
                try_and_create_atom_bond(atom_i, atom_j);
            }
        }
    }

    return bonds;
}

/*
static inline bool idx_in_residue(int idx, const Residue& res) { return res.beg_atom_idx <= idx && idx < res.end_atom_idx; }

static bool residues_are_connected(Residue res_a, Residue res_b, const Array<Bond> bonds) {
        for (const auto& bond : bonds) {
                if (idx_in_residue(bond.idx_a, res_a) && idx_in_residue(bond.idx_b, res_b)) return true;
                if (idx_in_residue(bond.idx_b, res_a) && idx_in_residue(bond.idx_a, res_b)) return true;
        }
        return false;
}
*/

// DynamicArray<Bond> compute_residue_bonds(const Array<Residue> residues, const Array<Bond> bonds, Allocator* alloc) {
//	return {};
//}

// @NOTE this method is sub-optimal and can surely be improved...
// Residues should have no more than 2 potential connections to other residues.
DynamicArray<Chain> compute_chains(const Array<Residue> residues, const Array<Bond> bonds, const Array<int32> atom_residue_indices,
                                   Allocator* alloc) {
    DynamicArray<Bond> residue_bonds;

    if (atom_residue_indices) {
        for (const auto& bond : bonds) {
            if (atom_residue_indices[bond.idx_a] != atom_residue_indices[bond.idx_b]) {
                residue_bonds.push_back({atom_residue_indices[bond.idx_a], atom_residue_indices[bond.idx_b]});
            }
        }
    } else {
        ASSERT(false, "Not implemented Yeti");
    }

    DynamicArray<int> residue_chains(residues.count, -1);

    int curr_chain_idx = 0;
    int res_bond_idx = 0;
    for (int i = 0; i < residues.count; i++) {
        if (residue_chains[i] == -1) residue_chains[i] = curr_chain_idx++;
        for (; res_bond_idx < residue_bonds.count; res_bond_idx++) {
            const auto& res_bond = residue_bonds[res_bond_idx];
            if (i == res_bond.idx_a) {
                residue_chains[res_bond.idx_b] = residue_chains[res_bond.idx_a];
            } else if (res_bond.idx_a > i)
                break;
        }
    }

    DynamicArray<Chain> chains;
    curr_chain_idx = -1;
    for (int i = 0; i < residue_chains.count; i++) {
        if (residue_chains[i] != curr_chain_idx) {
            curr_chain_idx = residue_chains[i];
            Label lbl;
            lbl.length = snprintf(lbl.data, Label::MAX_LENGTH, "C%i", curr_chain_idx);
            chains.push_back({lbl, i, i});
        }
        chains.back().end_res_idx++;
    }

    return chains;
}

/*
DynamicArray<Backbone> compute_backbones(const Array<Residue> residues, const Array<Bond> bonds, Allocator& alloc) {

}

void compute_backbones(Array<Backbone> backbone_dst, const Array<Residue> residues, const Array<Bond> bonds) {

}
*/

DynamicArray<float> compute_atom_radii(const Array<Element> elements, Allocator* alloc) {
    DynamicArray<float> radii(elements.count, 0);
    compute_atom_radii(radii, elements);
    return radii;
}

void compute_atom_radii(Array<float> radii_dst, const Array<Element> elements) {
    ASSERT(radii_dst.count <= elements.count);
    for (int64 i = 0; i < radii_dst.count; i++) {
        radii_dst[i] = element::vdw_radius(elements[i]);
    }
}

DynamicArray<uint32> compute_atom_colors(const MoleculeStructure& mol, ColorMapping mapping, Allocator* alloc) {
    DynamicArray<uint32> colors(mol.atom_elements.count, 0xFFFFFFFF);
    compute_atom_colors(colors, mol, mapping);
    return colors;
}

void compute_atom_colors(Array<uint32> color_dst, const MoleculeStructure& mol, ColorMapping mapping) {
    // @TODO: Implement different mappings
    (void)mapping;

    // CPK
    switch (mapping) {
        case ColorMapping::CPK:
            for (int64 i = 0; i < color_dst.count; i++) {
                color_dst[i] = element::color(mol.atom_elements[i]);
            }
            break;
        case ColorMapping::RES_ID:
            // Color based on residues, not unique by any means.
            // Perhaps use predefined colors if residues are Amino acids
            for (int64 i = 0; i < color_dst.count; i++) {
                if (i < mol.atom_residue_indices.count) {
                    const auto& res = mol.residues[mol.atom_residue_indices[i]];
                    unsigned int h = hash::crc32(res.id);
                    float hue = (h % 32) / 32.f;
                    vec3 c = math::hcl_to_rgb(vec3(hue, 0.8f, 0.8f));
                    unsigned char color[4];
                    color[0] = (uint8)(c.x * 255);
                    color[1] = (uint8)(c.y * 255);
                    color[2] = (uint8)(c.z * 255);
                    color[3] = 255;

                    color_dst[i] = *(uint32*)(color);
                }
            }
            break;
        case ColorMapping::RES_INDEX:
            for (int64 i = 0; i < color_dst.count; i++) {
                if (i < mol.atom_residue_indices.count) {
                    unsigned int h = hash::crc32(mol.atom_residue_indices[i]);
                    float hue = (h % 15) / 15.f;
                    vec3 c = math::hcl_to_rgb(vec3(hue, 0.8f, 0.8f));
                    unsigned char color[4];
                    color[0] = c.x * 255;
                    color[1] = c.y * 255;
                    color[2] = c.z * 255;
                    color[3] = 255;
                    color_dst[i] = *(uint32*)(color);
                }
            }
            break;
        case ColorMapping::CHAIN_INDEX:
            for (int64 i = 0; i < color_dst.count; i++) {
                if (i < mol.atom_residue_indices.count) {
                    const auto& res = mol.residues[mol.atom_residue_indices[i]];
                    if (res.chain_idx < mol.chains.count) {
                        unsigned int h = hash::crc32(res.chain_idx);
                        float hue = (h % 32) / 32.f;
                        vec3 c = math::hcl_to_rgb(vec3(hue, 0.8f, 0.8f));
                        unsigned char color[4];
                        color[0] = c.x * 255;
                        color[1] = c.y * 255;
                        color[2] = c.z * 255;
                        color[3] = 255;
                        color_dst[i] = *(uint32*)(color);
                    }
                }
            }
        default:
            break;
    }
}

namespace draw {

static float compute_fovy(const mat4& proj_mat) {
	// x is 1.f / tan(fovy * 0.5f);
	float x = proj_mat[1][1];
	return math::atan(1.f / x) * 2.f;
}

static constexpr int VERTEX_BUFFER_SIZE = MEGABYTES(4);
static GLuint empty_vao = 0;
static GLuint vbo = 0;

namespace vdw {
static GLuint v_shader = 0;
static GLuint f_shader = 0;
static GLuint program = 0;

static GLuint ibo = 0;

static GLint uniform_loc_view_mat = -1;
static GLint uniform_loc_proj_mat = -1;
static GLint uniform_loc_fov = -1;

static const char* v_shader_src = R"(
#version 150 core

uniform mat4 u_view_mat;
uniform mat4 u_proj_mat;
uniform float u_fov;

uniform samplerBuffer u_tex_pos_rad;
uniform samplerBuffer u_tex_color;

out Fragment {
    flat vec4 color;
    flat vec4 view_sphere;
    smooth vec4 view_coord;
	flat vec4 picking_color;
} out_frag;

vec4 pack_u32(uint data) {
	return vec4(
        (data & uint(0x000000FF)) >> 0,
        (data & uint(0x0000FF00)) >> 8,
        (data & uint(0x00FF0000)) >> 16,
        (data & uint(0xFF000000)) >> 24) / 255.0;
}

// From Inigo Quilez!
void proj_sphere(in vec4 sphere, 
				 in float fov,
				 out vec2 axis_a,
				 out vec2 axis_b,
				 out vec2 center) {
	vec3  o = sphere.xyz;
    float r2 = sphere.w*sphere.w;
	float z2 = o.z*o.z;	
	float l2 = dot(o,o);
	
	// axis
	axis_a = fov*sqrt(-r2*(r2-l2)/((l2-z2)*(r2-z2)*(r2-z2)))*vec2( o.x,o.y);
	axis_b = fov*sqrt(-r2*(r2-l2)/((l2-z2)*(r2-z2)*(r2-l2)))*vec2(-o.y,o.x);
	center = -fov*o.z*o.xy/(z2-r2);
}

void main() {
	int VID = gl_VertexID;
	int IID = gl_InstanceID;
	vec2 uv = vec2(VID / 2, VID % 2) * 2.0 - 1.0; 

	vec4 pos_rad = texelFetch(u_tex_pos_rad, IID);
	vec4 color = texelFetch(u_tex_color, IID);

	vec3 pos = pos_rad.xyz;
	float rad = pos_rad.w;

    vec4 view_coord = u_view_mat * vec4(pos, 1.0);
    float len = length(view_coord.xyz);
    vec3 view_dir = view_coord.xyz / len;

    out_frag.color = color;
    out_frag.view_sphere = vec4(view_coord.xyz, rad);
	out_frag.picking_color = pack_u32(uint(IID));

	vec2 axis_a;
	vec2 axis_b;
	vec2 center;
	proj_sphere(vec4(view_coord.xyz, rad), u_fov, axis_a, axis_b, center);

    const float sqrt_two = sqrt(2.0);
	float scl = sqrt_two * rad;

    view_coord.xyz -= view_dir * rad;
	//view_coord.xy += axis_a * uv.x + axis_b * uv.y;
	view_coord.xy += uv * scl;

	float z = -u_proj_mat[2][2] - u_proj_mat[3][2] / view_coord.z;
	vec2 xy = center + axis_a * uv.x + axis_b * uv.y;
	vec4 proj_coord = vec4(xy, z, 1);

	//out_frag.view_coord = view_coord;
	//out_frag.view_coord = u_view_mat * vec4(proj_coord.xy, u_fov, 0);

	out_frag.view_coord = inverse(u_proj_mat) * proj_coord;
	out_frag.view_coord = out_frag.view_coord / out_frag.view_coord.w;

    gl_Position = proj_coord;
}
)";

static const char* f_shader_src = R"(
#version 150 core
#extension GL_ARB_conservative_depth : enable
#extension GL_ARB_explicit_attrib_location : enable

uniform mat4 u_proj_mat;
uniform float u_exposure = 1.0;

in Fragment {
    flat vec4 color;
    flat vec4 view_sphere;
    smooth vec4 view_coord;
	flat vec4 picking_color;
} in_frag;

#ifdef GL_EXT_conservative_depth
layout (depth_greater) out float gl_FragDepth;
#endif
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_picking_id;

float fresnel(float H_dot_V) {   
    const float n1 = 1.0;
    const float n2 = 1.5;
    const float R0 = pow((n1-n2)/(n1+n2), 2);

    return R0 + (1.0 - R0)*pow(1.0 - H_dot_V, 5);
}

vec3 srgb_to_rgb_approx(vec3 srgb) {
    return pow(srgb, vec3(2.2));
}

void main() {
    vec3 center = in_frag.view_sphere.xyz;
    float radius = in_frag.view_sphere.w;
    vec3 view_dir = -normalize(in_frag.view_coord.xyz);

    vec3 m = -center;
    vec3 d = -view_dir;
    float r = radius;
    float b = dot(m, d);
    float c = dot(m, m) - r*r;
    float discr = b*b - c;
    //if (discr < 0.0) discard;
    float t = -b -sqrt(discr);

    vec3 view_hit = d * t;
    vec3 view_normal = (view_hit - center) / radius;
    vec4 color = in_frag.color;

    // Compute Color
    const vec3 env_radiance = vec3(1.0);
    const vec3 dir_radiance = vec3(10.0);
    const vec3 L = normalize(vec3(1));
    const float spec_exp = 50.0;

    vec3 N = view_normal;
    vec3 V = view_dir;
    vec3 H = normalize(L + V);
    float H_dot_V = max(0.0, dot(H, V));
    float N_dot_H = max(0.0, dot(N, H));
    float N_dot_L = max(0.0, dot(N, L));
    float fr = fresnel(H_dot_V);

    vec3 diffuse = color.rgb * (env_radiance + N_dot_L * dir_radiance);
    vec3 specular = dir_radiance * pow(N_dot_H, spec_exp);

    color.rgb = mix(diffuse, specular, fr);

    vec4 coord = vec4(0, 0, view_hit.z, 1);
    coord = u_proj_mat * coord;
    coord = coord / coord.w;

    gl_FragDepth = coord.z * 0.5 + 0.5;
    out_color = vec4(color.rgb, color.a);
	out_picking_id = in_frag.picking_color;
}
)";

static void initialize() {
    constexpr int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];

    v_shader = glCreateShader(GL_VERTEX_SHADER);
    f_shader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(v_shader, 1, &v_shader_src, 0);
    glShaderSource(f_shader, 1, &f_shader_src, 0);

    glCompileShader(v_shader);
    if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, v_shader)) {
        printf("Error while compiling vdw vertex shader:\n%s\n", buffer);
    }

    glCompileShader(f_shader);
    if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, f_shader)) {
        printf("Error while compiling vdw fragment shader:\n%s\n", buffer);
    }

    program = glCreateProgram();
    glAttachShader(program, v_shader);
    glAttachShader(program, f_shader);
    glLinkProgram(program);
    if (gl::get_program_link_error(buffer, BUFFER_SIZE, program)) {
        printf("Error while linking vdw program:\n%s\n", buffer);
    }

    glDetachShader(program, v_shader);
    glDetachShader(program, f_shader);

    glDeleteShader(v_shader);
    glDeleteShader(f_shader);

    uniform_loc_view_mat = glGetUniformLocation(program, "u_view_mat");
    uniform_loc_proj_mat = glGetUniformLocation(program, "u_proj_mat");
    uniform_loc_fov = glGetUniformLocation(program, "u_fov");
}

static void shutdown() {
    if (program) glDeleteProgram(program);
}

}  // namespace vdw

namespace licorice {
struct Vertex {
    float32 position[3];
    uint32 color;
};

static GLuint vao = 0;
static GLuint ibo = 0;

static GLuint v_shader = 0;
static GLuint g_shader = 0;
static GLuint f_shader = 0;
static GLuint program = 0;

static GLint attrib_loc_pos = -1;
static GLint attrib_loc_col = -1;
static GLint uniform_loc_view_mat = -1;
static GLint uniform_loc_proj_mat = -1;
static GLint uniform_loc_radius_scl = -1;

static const char* v_shader_src = R"(
#version 150 core

uniform mat4 u_view_mat;

in vec3	 v_position;
in vec4  v_color;

out Vertex {
    flat vec4 color;
	flat uint picking_id;
} out_vert;

void main() {
    gl_Position = u_view_mat * vec4(v_position, 1.0);
    out_vert.color = v_color;
	out_vert.picking_id = uint(gl_VertexID);
}
)";

static const char* g_shader_src = R"(
#version 150 core

uniform mat4 u_proj_mat;
uniform float u_radius_scl = 1.0;

layout (lines) in;
layout (triangle_strip, max_vertices = 24) out;

in Vertex {
    flat vec4 color;
	flat uint picking_id;
} in_vert[];

out Fragment {
    flat vec4 color[2];
	flat vec4 picking_color[2];
    smooth vec3 view_pos;

	flat vec4  capsule_center_radius;
	flat vec4  capsule_axis_length;
} out_frag;

vec4 pack_u32(uint data) {
	return vec4(
        (data & uint(0x000000FF)) >> 0,
        (data & uint(0x0000FF00)) >> 8,
        (data & uint(0x00FF0000)) >> 16,
        (data & uint(0xFF000000)) >> 24) / 255.0;
}

vec4 prismoid[8];

void emit_vertex(int a){
    out_frag.view_pos = prismoid[a].xyz;
	gl_Position = u_proj_mat * prismoid[a];
    EmitVertex();
}

void emit(int a, int b, int c, int d)
{
    emit_vertex(a);
    emit_vertex(b);
    emit_vertex(c);
    emit_vertex(d);
    EndPrimitive(); 
}

vec3 get_ortho_vec(vec3 v, vec3 A, vec3 B){
    if(abs(1-dot(v,A))>0.001){
        return normalize(cross(v,A));
    }else{
        return normalize(cross(v,B));
    }
}

void main()
{
    if (in_vert[0].color.a == 0 || in_vert[1].color.a == 0) {
        EndPrimitive();
        return;
    }

    // Compute orientation vectors for the two connecting faces:
    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = gl_in[1].gl_Position.xyz;
	float r = 1.0 * u_radius_scl;
	float l = distance(p0, p1);
	vec3 a = (p1 - p0) / l;
	vec3 c = (p0 + p1) * 0.5;

	out_frag.color[0] = in_vert[0].color;
	out_frag.color[1] = in_vert[1].color;

    out_frag.picking_color[0] = pack_u32(in_vert[0].picking_id);
    out_frag.picking_color[1] = pack_u32(in_vert[1].picking_id);

	out_frag.capsule_center_radius = vec4(c, r);
	out_frag.capsule_axis_length = vec4(a, l);

    // Extend end points to properly fit the sphere caps
    p0 -= a * r;
    p1 += a * r;

	vec3 B = vec3(0,0,1);
	vec3 A = vec3(1,0,0);
    vec3 o = get_ortho_vec(a,A,B);

    // Declare scratch variables for basis vectors:
    vec3 i,j,k;

    // Compute vertices of prismoid:
    j = a; i = o; k = normalize(cross(i, j)); i = normalize(cross(k, j)); ; i *= r; k *= r;
    prismoid[0] = vec4(p0 + i + k, 1);
    prismoid[1] = vec4(p0 + i - k, 1);
    prismoid[2] = vec4(p0 - i - k, 1);
    prismoid[3] = vec4(p0 - i + k, 1);
    prismoid[4] = vec4(p1 + i + k, 1);
    prismoid[5] = vec4(p1 + i - k, 1);
    prismoid[6] = vec4(p1 - i - k, 1);
    prismoid[7] = vec4(p1 - i + k, 1);

    // Emit the six faces of the prismoid:
    emit(0,1,3,2); emit(5,4,6,7);
    emit(4,5,0,1); emit(3,2,7,6);
    emit(0,3,4,7); emit(2,1,6,5);
}
)";

static const char* f_shader_src = R"(
#version 150 core
#extension GL_ARB_conservative_depth : enable
#extension GL_ARB_explicit_attrib_location : enable

uniform mat4 u_proj_mat;
uniform float u_exposure = 1.0;
uniform float u_radius_scl = 1.0;

in Fragment {
    flat vec4 color[2];
	flat vec4 picking_color[2];
    smooth vec3 view_pos;

	flat vec4  capsule_center_radius;
	flat vec4  capsule_axis_length;
} in_frag;

#ifdef GL_EXT_conservative_depth
layout (depth_greater) out float gl_FragDepth;
#endif
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_picking_id;

// Source from Ingo Quilez (https://www.shadertoy.com/view/Xt3SzX)
float intersect_capsule(in vec3 ro, in vec3 rd, in vec3 cc, in vec3 ca, float cr,
                      float ch, out vec3 normal, out int side)  // cc center, ca orientation axis, cr radius, ch height
{
    vec3 oc = ro - cc;
    ch *= 0.5;

    float card = dot(ca, rd);
    float caoc = dot(ca, oc);

    float a = 1.0 - card * card;
    float b = dot(oc, rd) - caoc * card;
    float c = dot(oc, oc) - caoc * caoc - cr * cr;
    float h = b * b - a * c;
    if (h < 0.0) return -1.0;
    float t = (-b - sqrt(h)) / a;

    float y = caoc + t * card;
    side = int(y > 0);

    // body
    if (abs(y) < ch) {
        normal = normalize(oc + t * rd - ca * y);
        return t;
    }

    // caps
    float sy = sign(y);
    oc = ro - (cc + sy * ca * ch);
    b = dot(rd, oc);
    c = dot(oc, oc) - cr * cr;
    h = b * b - c;
    if (h > 0.0) {
        t = -b - sqrt(h);
        normal = normalize(oc + rd * t);
        return t;
    }

    return -1.0;
}

vec4 pack_u32(uint data) {
	return vec4(
        (data & uint(0x000000FF)) >> 0,
        (data & uint(0x0000FF00)) >> 8,
        (data & uint(0x00FF0000)) >> 16,
        (data & uint(0xFF000000)) >> 24) / 255.0;
}

float fresnel(float H_dot_V) {   
    const float n1 = 1.0;
    const float n2 = 1.5;
    const float R0 = pow((n1-n2)/(n1+n2), 2);

    return R0 + (1.0 - R0)*pow(1.0 - H_dot_V, 5);
}

void main() {
    vec3 ro = vec3(0);
    vec3 rd = normalize(in_frag.view_pos);
	vec3 cc = in_frag.capsule_center_radius.xyz;
	float cr = in_frag.capsule_center_radius.w;
	vec3 ca = in_frag.capsule_axis_length.xyz;
    float ch = in_frag.capsule_axis_length.w;

    vec3 normal;
    int side;
    float t = intersect_capsule(ro, rd, cc, ca, cr, ch, normal, side);
    if (t < 0.0) {
        discard;
        return;
    }

    vec3 pos = ro + t*rd;
    vec4 color = in_frag.color[side];
	vec4 picking_color = in_frag.picking_color[side];

    // Compute Color
    const vec3 env_radiance = vec3(1.0);
    const vec3 dir_radiance = vec3(10.0);
    const vec3 L = normalize(vec3(1));
    const float spec_exp = 50.0;

    vec3 N = normal;
    vec3 V = -rd;
    vec3 H = normalize(L + V);
    float H_dot_V = max(0.0, dot(H, V));
    float N_dot_H = max(0.0, dot(N, H));
    float N_dot_L = max(0.0, dot(N, L));
    float fr = fresnel(H_dot_V);

    vec3 diffuse = color.rgb * (env_radiance + N_dot_L * dir_radiance);
    vec3 specular = dir_radiance * pow(N_dot_H, spec_exp);

    color.rgb = mix(diffuse, specular, fr);

    vec4 coord = vec4(0, 0, pos.z, 1);
    coord = u_proj_mat * coord;
    coord = coord / coord.w;

    gl_FragDepth = coord.z * 0.5 + 0.5;
    out_color = vec4(color.rgb, color.a);
	out_picking_id = picking_color;
}
)";

static void initialize() {
    constexpr int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];

    v_shader = glCreateShader(GL_VERTEX_SHADER);
    g_shader = glCreateShader(GL_GEOMETRY_SHADER);
    f_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v_shader, 1, &v_shader_src, 0);
    glShaderSource(g_shader, 1, &g_shader_src, 0);
    glShaderSource(f_shader, 1, &f_shader_src, 0);

    glCompileShader(v_shader);
    if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, v_shader)) {
        printf("Error while compiling licorice vertex shader:\n%s\n", buffer);
    }
    glCompileShader(g_shader);
    if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, g_shader)) {
        printf("Error while compiling licorice geometry shader:\n%s\n", buffer);
    }
    glCompileShader(f_shader);
    if (gl::get_shader_compile_error(buffer, BUFFER_SIZE, f_shader)) {
        printf("Error while compiling licorice fragment shader:\n%s\n", buffer);
    }

    program = glCreateProgram();
    glAttachShader(program, v_shader);
    glAttachShader(program, g_shader);
    glAttachShader(program, f_shader);
    glLinkProgram(program);
    if (gl::get_program_link_error(buffer, BUFFER_SIZE, program)) {
        printf("Error while linking licorice program:\n%s\n", buffer);
    }

    glDetachShader(program, v_shader);
    glDetachShader(program, g_shader);
    glDetachShader(program, f_shader);

    glDeleteShader(v_shader);
    glDeleteShader(g_shader);
    glDeleteShader(f_shader);

    attrib_loc_pos = glGetAttribLocation(program, "v_position");
    attrib_loc_col = glGetAttribLocation(program, "v_color");
    uniform_loc_view_mat = glGetUniformLocation(program, "u_view_mat");
    uniform_loc_proj_mat = glGetUniformLocation(program, "u_proj_mat");
    uniform_loc_radius_scl = glGetUniformLocation(program, "u_radius_scl");

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(attrib_loc_pos);
    glVertexAttribPointer(attrib_loc_pos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const GLvoid*)0);

    glEnableVertexAttribArray(attrib_loc_col);
    glVertexAttribPointer(attrib_loc_col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (const GLvoid*)12);

    glBindVertexArray(0);

	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, VERTEX_BUFFER_SIZE, nullptr, GL_STREAM_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

static void shutdown() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (program) glDeleteProgram(program);
	if (ibo) glDeleteBuffers(1, &ibo);
}

}  // namespace licorice

void initialize() {
	glGenVertexArrays(1, &empty_vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, VERTEX_BUFFER_SIZE, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    vdw::initialize();
	licorice::initialize();
}

void shutdown() {
	if (empty_vao) glDeleteVertexArrays(1, &empty_vao);
    if (vbo) glDeleteBuffers(1, &vbo);

    vdw::shutdown();
	licorice::shutdown();
}

void draw_vdw(const Array<vec3> atom_positions, const Array<float> atom_radii, const Array<uint32> atom_colors,
              const mat4& view_mat, const mat4& proj_mat, float radii_scale) {
    int64_t count = atom_positions.count;
    ASSERT(count == atom_radii.count && count == atom_colors.count);

	static GLuint ibo = 0;
	static GLuint buf_position_radius = 0;
	static GLuint buf_color = 0;
	static GLuint tex_position_radius = 0;
	static GLuint tex_color = 0;
	static GLint uniform_loc_tex_pos_rad = -1;
	static GLint uniform_loc_tex_color = -1;

	if (!ibo) {
		const unsigned char data[4] = { 0, 1, 2, 3 };
		glGenBuffers(1, &ibo);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, 4, data, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	if (!buf_position_radius) {
		glGenBuffers(1, &buf_position_radius);
		glBindBuffer(GL_ARRAY_BUFFER, buf_position_radius);
		glBufferData(GL_ARRAY_BUFFER, MEGABYTES(4), 0, GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	if (!buf_color) {
		glGenBuffers(1, &buf_color);
		glBindBuffer(GL_ARRAY_BUFFER, buf_color);
		glBufferData(GL_ARRAY_BUFFER, MEGABYTES(4), 0, GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	if (!tex_position_radius)
		glGenTextures(1, &tex_position_radius);

	if (!tex_color)
		glGenTextures(1, &tex_color);

	if (uniform_loc_tex_pos_rad == -1)
		uniform_loc_tex_pos_rad = glGetUniformLocation(vdw::program, "u_tex_pos_rad");

	if (uniform_loc_tex_color == -1)
		uniform_loc_tex_color = glGetUniformLocation(vdw::program, "u_tex_color");

	unsigned int draw_count = 0;

	{
		glBindBuffer(GL_ARRAY_BUFFER, buf_position_radius);
		vec4* gpu_pos_rad = (vec4*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		glBindBuffer(GL_ARRAY_BUFFER, buf_color);
		uint32*	gpu_color = (uint32*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

		// @ TODO: DISCARD ANY ZERO RADII OR ZERO COLOR ALPHA ATOMS HERE
		for (int64_t i = 0; i < count; i++) {
			if (atom_radii[i] <= 0.f) continue;
			if (atom_colors[i] & 0xff000000 == 0) continue;
			gpu_pos_rad[i] = vec4(atom_positions[i], atom_radii[i] * radii_scale);
			gpu_color[i] = atom_colors[i];
			draw_count++;
		}

		glUnmapBuffer(GL_ARRAY_BUFFER);
		glBindBuffer(GL_ARRAY_BUFFER, buf_position_radius);
		glUnmapBuffer(GL_ARRAY_BUFFER);
	}

    glEnable(GL_DEPTH_TEST);

    glBindVertexArray(empty_vao);
    glUseProgram(vdw::program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_BUFFER, tex_position_radius);
	glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, buf_position_radius);
	glUniform1i(uniform_loc_tex_pos_rad, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_BUFFER, tex_color);
	glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA8, buf_color);
	glUniform1i(uniform_loc_tex_color, 1);

    glUniformMatrix4fv(vdw::uniform_loc_view_mat, 1, GL_FALSE, &view_mat[0][0]);
    glUniformMatrix4fv(vdw::uniform_loc_proj_mat, 1, GL_FALSE, &proj_mat[0][0]);
	glUniform1f(vdw::uniform_loc_fov, compute_fovy(proj_mat));
    //glUniform1f(vdw::uniform_loc_radius_scl, radii_scale);
    //glDrawArrays(GL_POINTS, 0, (GLsizei)count);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glDrawElementsInstanced(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, 0, draw_count);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glDisable(GL_DEPTH_TEST);
}

void draw_licorice(const Array<vec3> atom_positions, const Array<Bond> atom_bonds, const Array<uint32> atom_colors,
                   const mat4& view_mat, const mat4& proj_mat, float radii_scale) {
	int64_t count = atom_positions.count;
	ASSERT(count == atom_colors.count);
	ASSERT(count * sizeof(licorice::Vertex) < VERTEX_BUFFER_SIZE);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	licorice::Vertex* data = (licorice::Vertex*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	for (int64_t i = 0; i < count; i++) {
		data[i].position[0] = atom_positions[i][0];
		data[i].position[1] = atom_positions[i][1];
		data[i].position[2] = atom_positions[i][2];
		data[i].color = atom_colors[i];
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, licorice::ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, atom_bonds.count * sizeof(Bond), atom_bonds.data, GL_STREAM_DRAW);

	glEnable(GL_DEPTH_TEST);

	glBindVertexArray(licorice::vao);
	glUseProgram(licorice::program);
	glUniformMatrix4fv(licorice::uniform_loc_view_mat, 1, GL_FALSE, &view_mat[0][0]);
	glUniformMatrix4fv(licorice::uniform_loc_proj_mat, 1, GL_FALSE, &proj_mat[0][0]);
	glUniform1f(licorice::uniform_loc_radius_scl, radii_scale);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, licorice::ibo);
	glDrawElements(GL_LINES, (GLsizei)atom_bonds.count * 2, GL_UNSIGNED_INT, (const void*)0);
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glDisable(GL_DEPTH_TEST);
}

}  // namespace draw