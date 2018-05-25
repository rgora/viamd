#include "molecule_structure.h"

bool init_molecule_structure(MoleculeStructure* mol, int32 num_atoms, int32 num_bonds, int32 num_residues, int32 num_chains, int32 num_backbone_segments, int32 num_hydrogen_bond_donors, int32 num_hydrogen_bond_acceptors) {
	free_molecule_structure(mol);

	int64 alloc_size = 0;

	alloc_size += num_atoms * (sizeof(vec3) + sizeof(Element) + sizeof(Label) + sizeof(ResIdx));
	alloc_size += num_bonds * sizeof(Bond);
	alloc_size += num_residues * sizeof(Residue);
	alloc_size += num_chains * sizeof(Chain);
	alloc_size += num_backbone_segments * sizeof(BackboneSegment);
    alloc_size += num_hydrogen_bond_donors * sizeof(HydrogenBondDonor);
    alloc_size += num_hydrogen_bond_acceptors * sizeof(HydrogenBondAcceptor);

	void* data = MALLOC(alloc_size);

	mol->atom_positions =			{ (vec3*)data, num_atoms };
	mol->atom_elements =			{ (Element*)(mol->atom_positions.end()), num_atoms };
	mol->atom_labels =				{ (Label*)(mol->atom_elements.end()), num_atoms };
	mol->atom_residue_indices =		{ (ResIdx*)(mol->atom_labels.end()), num_atoms };
	
	mol->covalent_bonds =			{ (Bond*)(mol->atom_residue_indices.end()), num_bonds };
	mol->residues =					{ (Residue*)(mol->covalent_bonds.end()), num_residues };
	mol->chains =					{ (Chain*)(mol->residues.end()), num_chains };
	mol->backbone_segments =		{ (BackboneSegment*)(mol->chains.end()), num_backbone_segments };
    mol->hydrogen_bond.donors =		{ (HydrogenBondDonor*)(mol->backbone_segments.end()), num_hydrogen_bond_donors};
    mol->hydrogen_bond.acceptors =	{ (HydrogenBondAcceptor*)(mol->hydrogen_bond.donors.end()), num_hydrogen_bond_acceptors};

	return true;
}

void free_molecule_structure(MoleculeStructure* mol) {
	ASSERT(mol);
	if (mol->atom_positions.beg()) FREE(mol->atom_positions.beg());
	*mol = {};
}