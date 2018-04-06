#include "stats.h"
#include <core/math_utils.h>
#include <core/hash.h>
#include <mol/molecule_dynamic.h>
#include <mol/molecule_utils.h>

#define COMPUTE_ID(x) (hash::crc64(x))

namespace stats {

struct PropertyCommand {
    ID id;
    PropertyComputeFunc func;
    Range val_range;
    PropertyType type;
    bool periodic;
    CString unit;
};

struct GroupCommand {
    ID id;
    StructureExtractFunc func;
};

struct Property {
    ID id = INVALID_ID;
    ID data_avg_id = INVALID_ID;
    ID data_beg_id = INVALID_ID;
    int32 data_count = 0;

	float filter_min = 0.f;
	float filter_max = 1.f;

    ID cmd_id = INVALID_ID;
    CString name;
    CString args;
};

struct GroupInstance {
	ID id = INVALID_ID;
	ID group_id = INVALID_ID;
	Structure structure;
};

struct PropertyData {
    ID id = INVALID_ID;
    ID property_id = INVALID_ID;
	ID instance_id = INVALID_ID;

    Array<float> data;
};

struct Group {
    ID id = INVALID_ID;

	ID instance_beg_id = INVALID_ID;
	int32 instance_count = 0;

    ID cmd_id = INVALID_ID;
    CString name;
    CString args;
};

struct StatisticsContext {
    DynamicArray<String> string_buffer {};

    DynamicArray<Property> properties{};
    DynamicArray<PropertyData> property_data{};
    DynamicArray<Group> groups{};
	DynamicArray<GroupInstance> group_instances{};

    DynamicArray<PropertyCommand> property_commands;
    DynamicArray<GroupCommand> group_commands;
};

static StatisticsContext ctx;

static bool compute_atomic_distance(float* data, const Array<CString> args, const MoleculeDynamic& dynamic, Structure group_struct) {
	if (args.count != 2) return false;
    if (group_struct.beg_atom_idx == group_struct.end_atom_idx) return false;

	auto int_a = to_int32(args[0]);
	auto int_b = to_int32(args[1]);

	if (!int_a.success || !int_b.success) return false;
	int32 atom_a = group_struct.beg_atom_idx + int_a;
	int32 atom_b = group_struct.beg_atom_idx + int_b;

    int32 count = dynamic.trajectory->num_frames;
    for (int32 i = 0; i < count; i++) {
        vec3 pos_a = dynamic.trajectory->frame_buffer[i].atom_positions[atom_a];
        vec3 pos_b = dynamic.trajectory->frame_buffer[i].atom_positions[atom_b];
        data[i] = math::distance(pos_a, pos_b);
    }

    return true;
}

static bool compute_atomic_angle(float* data, const Array<CString> args, const MoleculeDynamic& dynamic, Structure group_struct) {
	if (args.count != 3) return false;
    if (group_struct.beg_atom_idx == group_struct.end_atom_idx) return false;

	auto int_a = to_int32(args[0]);
	auto int_b = to_int32(args[1]);
	auto int_c = to_int32(args[2]);

	if (!int_a.success || !int_b.success || !int_c.success) return false;
	int32 atom_a = group_struct.beg_atom_idx + int_a;
	int32 atom_b = group_struct.beg_atom_idx + int_b;
	int32 atom_c = group_struct.beg_atom_idx + int_c;

	int32 count = dynamic.trajectory->num_frames;
	for (int32 i = 0; i < count; i++) {
		vec3 pos_a = dynamic.trajectory->frame_buffer[i].atom_positions[atom_a];
		vec3 pos_b = dynamic.trajectory->frame_buffer[i].atom_positions[atom_b];
		vec3 pos_c = dynamic.trajectory->frame_buffer[i].atom_positions[atom_c];

		data[i] = math::angle(pos_a - pos_b, pos_c - pos_b);
	}

	return true;
}

static bool compute_atomic_dihedral(float* data, const Array<CString> args, const MoleculeDynamic& dynamic, Structure group_struct) {
	if (args.count != 4) return false;
    if (group_struct.beg_atom_idx == group_struct.end_atom_idx) return false;

	auto int_a = to_int32(args[0]);
	auto int_b = to_int32(args[1]);
	auto int_c = to_int32(args[2]);
	auto int_d = to_int32(args[2]);

	if (!int_a.success || !int_b.success || !int_c.success || !int_d.success) return false;
    int32 atom_a = group_struct.beg_atom_idx + int_a;
    int32 atom_b = group_struct.beg_atom_idx + int_b;
    int32 atom_c = group_struct.beg_atom_idx + int_c;
    int32 atom_d = group_struct.beg_atom_idx + int_d;

	int32 count = dynamic.trajectory->num_frames;
	for (int32 i = 0; i < count; i++) {
		vec3 pos_a = dynamic.trajectory->frame_buffer[i].atom_positions[atom_a];
		vec3 pos_b = dynamic.trajectory->frame_buffer[i].atom_positions[atom_b];
		vec3 pos_c = dynamic.trajectory->frame_buffer[i].atom_positions[atom_c];
		vec3 pos_d = dynamic.trajectory->frame_buffer[i].atom_positions[atom_d];

		data[i] = dihedral_angle(pos_a, pos_b, pos_c, pos_d);
	}

	return true;
}

static DynamicArray<Structure> match_by_resname(const Array<CString> args, const MoleculeStructure& mol) {
    DynamicArray<Structure> result;
    for (const auto& res : mol.residues) {
        for (const auto& arg : args) {
            if (compare(res.name, arg)) {
                result.push_back({res.beg_atom_idx, res.end_atom_idx});
            }
        }
    }
    return result;
}

void initialize() {
    ctx.property_commands.push_back({ COMPUTE_ID("dist"),	  compute_atomic_distance, {0, FLT_MAX}, INTRA, false, "å" });
	ctx.property_commands.push_back({ COMPUTE_ID("bond"),	  compute_atomic_distance, {0, FLT_MAX}, INTRA, false, "å" });
	ctx.property_commands.push_back({ COMPUTE_ID("angle"),	  compute_atomic_angle, {0, math::PI}, INTRA, true, "°" });
	ctx.property_commands.push_back({ COMPUTE_ID("dihedral"), compute_atomic_dihedral, {-math::PI, math::PI}, INTRA, true, "°" });

    //ctx.group_commands.push_back({ COMPUTE_ID("resid"), match_by_resname});
    ctx.group_commands.push_back({ COMPUTE_ID("resname"), match_by_resname});
}

void shutdown() {

}

void clear() {
    for (auto str : ctx.string_buffer) {
        FREE(&str.data);
    }
    ctx.string_buffer.clear();
    ctx.properties.clear();
    ctx.property_data.clear();
    ctx.groups.clear();
	ctx.group_instances.clear();

    //ctx.property_commands.clear();
    //ctx.group_commands.clear();
}

template <typename T>
static T* find_id(Array<T> data, ID id) {
    for (auto& item : data) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

static CString alloc_string(CString str) {
    char* data = (char*)MALLOC(str.count);
    ctx.string_buffer.push_back({data, str.count});
	copy(ctx.string_buffer.back(), str);
    return ctx.string_buffer.back();
}

static void free_string(CString str) {
    for (auto int_str : ctx.string_buffer)  {
        if (int_str.count == str.count && compare(int_str, str)) {
            ctx.string_buffer.remove(&int_str);
            return;
        }
    }
}

// HISTOGRAMS
Histogram compute_histogram(int32 num_bins, Array<float> data) {
    Histogram hist;
    compute_histogram(&hist, num_bins, data);
    return hist;
}

Histogram compute_histogram(int32 num_bins, Array<float> data, float min_val, float max_val) {
    Histogram hist;
    compute_histogram(&hist, num_bins, data, min_val, max_val);
    return hist;
}

void compute_histogram(Histogram* hist, int32 num_bins, Array<float> data) {
    ASSERT(hist);
    if (data.count == 0) return;
    float min_val = FLT_MAX;
    float max_val = -FLT_MAX;
    for (const auto& d : data) {
        min_val = math::min(min_val, d);
        max_val = math::max(max_val, d);
    }
    compute_histogram(hist, num_bins, data, min_val, max_val);
}

void compute_histogram(Histogram* hist, int32 num_bins, Array<float> data, float min_val, float max_val) {
    ASSERT(hist);
	ASSERT(num_bins > 0);

	hist->bins.resize(num_bins);
	memset(hist->bins.data, 0, hist->bins.count * sizeof(float));

	const float scl = num_bins / (max_val - min_val);
	for (auto v : data) {
		int32 bin_idx = math::clamp((int32)((v - min_val) * scl), 0, num_bins - 1);
		hist->bins[bin_idx]++;
	}
}

bool compute_stats(MoleculeDynamic* dynamic) {
    ASSERT(dynamic);
    if (!dynamic->molecule) {
        printf("ERROR! Computing statistics: molecule is not set");
        return false;
    }
    if (!dynamic->trajectory) {
        printf("ERROR! Computing statistics: trajectory is not set");
        return false;
    }

    // Find uninitialized groups and compute their instances
    for (auto& group : ctx.groups) {
		if (group.instance_count == 0) {
            GroupCommand* group_cmd = find_id(ctx.group_commands, group.cmd_id);
			ASSERT(group_cmd);
            DynamicArray<CString> args = ctokenize(group.args);
            auto matching_structures = group_cmd->func(args, *dynamic->molecule);

			if (matching_structures.count == 0) {
				printf("WARNING! group '%s' did not match any structures.\n");
				continue;
			}

			for (int32 i = 0; i < matching_structures.count; i++) {
				char buf[64];
				snprintf(buf, 64, "%s.%i", group.name.beg(), i);
				GroupInstance instance;
				instance.id = COMPUTE_ID(buf);
				instance.group_id = group.id;
				instance.structure = matching_structures[i];
				ctx.group_instances.push_back(instance);

				if (i == 0) {
					group.instance_beg_id = instance.id;
					group.instance_count = 0;
				}
				group.instance_count++;
			}
        }
    }

    int32 count = dynamic->trajectory->num_frames;
    for (auto& prop : ctx.properties) {
        if (prop.data_beg_id == INVALID_ID) {
            // NEED TO COMPUTE PROPERTY DATA
            PropertyCommand* prop_cmd = find_id(ctx.property_commands, prop.cmd_id);
			ASSERT(prop_cmd);
            DynamicArray<CString> args = ctokenize(prop.args);

			if (prop_cmd->type == INTRA) {
				if (args == 0) {
					printf("WARNING! Property '%s': Missing arguments!", prop.name.beg());
					continue;
				}
				ID group_id = get_group(args[0]);
				Group* group = find_id(ctx.groups, group_id);

				if (group_id == INVALID_ID) {
					printf("WARNING! Property '%s': could not find group with name '%s'", prop.name.beg(), args[0].beg());
					continue;
				}

				// DATA AVERAGE
				StringBuffer<64> prop_data_name;
				snprintf(prop_data_name.beg(), 64, "%s.%s.avg", group->name.beg(), prop.name.beg());

				PropertyData prop_avg_data;
				prop_avg_data.id = COMPUTE_ID(prop_data_name.operator CString());
				prop_avg_data.instance_id = INVALID_ID;
				prop_avg_data.property_id = prop.id;
				prop_avg_data.data = { (float*)CALLOC(count, sizeof(float)), count };
				prop.data_avg_id = prop_avg_data.id;
				ctx.property_data.push_back(prop_avg_data);

				// DATA
				for (int32 i = 0; i < group->instance_count; i++) {
					ID instance_id = get_group_instance(group_id, i);
					auto inst = find_id(ctx.group_instances, instance_id);
					float* data = (float*)CALLOC(count, sizeof(float));
					prop_cmd->func(data, args, *dynamic, inst->structure);

					for (int32 j = 0; j < count; j++) {
						prop_avg_data.data[j] += data[j] / (float)group->instance_count;
					}

					snprintf(prop_data_name.beg(), 64, "%s.%s.%i", group->name.beg(), prop.name.beg(), i);

					PropertyData prop_data;
					prop_data.id = COMPUTE_ID(prop_data_name);
					prop_data.instance_id = instance_id;
					prop_data.property_id = prop.id;
					prop_data.data = { data, count };

					ctx.property_data.push_back(prop_data);
					if (i == 0) {
						prop.data_beg_id = prop_data.id;
						prop.data_count = 0;
					}
					prop.data_count++;
				}
			}
			else if (prop_cmd->type == INTER) {

			}

        }
    }

    return true;
}

void register_property_command(CString command, PropertyCommandDescriptor desc) {
    ID id = COMPUTE_ID(command);
    auto cmd = find_id(ctx.property_commands, id);
    if (cmd != nullptr) {
        printf("ERROR: PROPERTY COMMAND %s ALREADY REGISTERED!", command.beg());
        return;
    }

    auto unit = alloc_string(desc.unit);
    ctx.property_commands.push_back({id, desc.compute_function, desc.val_range, desc.type, desc.periodic, unit});
}

void register_group_command(CString command, StructureExtractFunc func) {
    ID id = COMPUTE_ID(command);
    auto cmd = find_id(ctx.group_commands, id);
    if (cmd != nullptr) {
        printf("ERROR: GROUP COMMAND %s ALREADY REGISTERED!", command.beg());
        return;
    }

    ctx.group_commands.push_back({id, func});
}

ID create_group(CString name, CString cmd_and_args) {
	ID grp_id = COMPUTE_ID(name);
	Group* grp = find_id(ctx.groups, grp_id);
	if (grp != nullptr) {
		StringBuffer<32> buf = name;
		printf("ERROR: GROUP '%s' ALREADY REGISTERED!", buf.beg());
		return INVALID_ID;
	}
	
	if (cmd_and_args.count == 0) {
		printf("ERROR: command and arguments is missing\n");
		return INVALID_ID;
	}

	auto tokens = ctokenize(cmd_and_args);
	CString cmd = tokens[0];
	CString args = "";

	if (tokens.count > 1) {
		args = { tokens[1].beg(), tokens.back().end() };
	}

	ID grp_cmd_id = COMPUTE_ID(cmd);
	GroupCommand* grp_cmd = find_id(ctx.group_commands, grp_cmd_id);
	if (grp_cmd == nullptr) {
		StringBuffer<32> buf = cmd;
		printf("ERROR: UNIDENTIFIED GROUP COMMAND '%s'!", buf.beg());
		return INVALID_ID;
	}

	Group group;
	group.id = grp_id;
	group.name = alloc_string(name);
    group.args = alloc_string(args);
	group.cmd_id = grp_cmd_id;
	group.instance_beg_id = INVALID_ID;
	group.instance_count = 0;

	ctx.groups.push_back(group);

	return group.id;
}

void remove_group(ID group_id) {
	Group* group = find_id(ctx.groups, group_id);
	if (!group) {
		printf("ERROR: COULD NOT FIND GROUP!\n");
		return;
	}

	free_string(group->name);
	free_string(group->args);

    ctx.groups.remove(group);
}

ID get_group(CString name) {
    for (const auto& g : ctx.groups) {
        if (compare(name, g.name)) return g.id;
    }
    return INVALID_ID;
}

ID get_group(int32 idx) {
    if (idx < ctx.groups.count) {
        return ctx.groups[idx].id;
    }
    return INVALID_ID;
}

int32 get_group_count() {
    return (int32)ctx.groups.count;
}

ID get_group_instance(ID group_id, int32 idx) {
	int32 count = 0;
	for (const auto &inst : ctx.group_instances) {
		if (inst.group_id == group_id) {
			if (count == idx) return inst.id;
			count++;
		}
	}
	return INVALID_ID;
}

int32 get_group_instance_count(ID group_id) {
	auto group = find_id(ctx.groups, group_id);
	if (group) {
		return group->instance_count;
	}
	return 0;
}

ID get_property(CString name) {
    for (const auto &p : ctx.properties) {
        if (compare(p.name, name)) return p.id;
    }
    return INVALID_ID;
}

ID get_property(int32 idx) {
	if (-1 < idx && idx < ctx.properties.count) return ctx.properties[idx].id;
    return INVALID_ID;
}

ID create_property(CString name, CString cmd_and_args) {
    ID prop_id = COMPUTE_ID(name);
    Property* old_prop = find_id(ctx.properties, prop_id);
    if (old_prop != nullptr) {
        StringBuffer<32> buf = name;
        printf("ERROR: PROPERTY '%s' ALREADY EXISTS!", buf.beg());
        return INVALID_ID;
    }

	if (cmd_and_args.count == 0) {
		printf("ERROR: command and arguments is missing\n");
		return INVALID_ID;
	}

	auto tokens = ctokenize(cmd_and_args);
	CString cmd = tokens[0];
	CString args = "";

	if (tokens.count > 1) {
		args = { tokens[1].beg(), tokens.back().end() };
	}

    ID prop_cmd_id = COMPUTE_ID(cmd);
    PropertyCommand* prop_cmd = find_id(ctx.property_commands, prop_cmd_id);
    if (prop_cmd == nullptr) {
        StringBuffer<32> cmd_buf = cmd;
        printf("ERROR: UNIDENTIFIED PROPERTY COMMAND '%s'!", cmd_buf.beg());
        return INVALID_ID;
    }

    Property prop;
    prop.id = prop_id;
    prop.data_beg_id = INVALID_ID;
    prop.data_count = 0;

    prop.cmd_id = prop_cmd_id;
    prop.name = alloc_string(name);
    prop.args = alloc_string(args);

	return prop.id;
}

void remove_property(ID prop_id) {
    Property* prop = find_id(ctx.properties, prop_id);
    if (prop == nullptr) {
        printf("ERROR: PROPERTY NOT FOUND\n");
        return;
    }

    for (PropertyData* pd = ctx.property_data.beg(); pd != ctx.property_data.end(); pd++) {
        if (pd->property_id == prop_id) ctx.property_data.remove(pd);
    }

    free_string(prop->name);
    free_string(prop->args);

    ctx.properties.remove(prop);
}

Array<float> get_property_data(ID prop_id, int32 idx) {
    auto prop = find_id(ctx.properties, prop_id);
    if (prop && prop->data_beg_id != INVALID_ID && idx < prop->data_count) {
        auto prop_data = find_id(ctx.property_data, prop->data_beg_id);
        return prop_data[idx].data;
    }
    return {};
}

Array<float> get_property_avg_data(ID prop_id) {
    auto prop = find_id(ctx.properties, prop_id);
    if (prop && prop->data_avg_id != INVALID_ID) {
        auto prop_data = find_id(ctx.property_data, prop->data_avg_id);
        return prop_data->data;
    }
    return {};
}

int32 get_property_data_count(ID prop_id) {
    auto prop = find_id(ctx.properties, prop_id);
    if (prop) {
        return prop->data_count;
    }
	return 0;
}

CString	get_property_name(ID prop_id) {
	auto prop = find_id(ctx.properties, prop_id);
	if (prop) {
		return prop->name;
	}
	return {};
}

}  // namespace stats