/*
    Copyright 2017 Zheyong Fan, Ville Vierimaa, Mikko Ervasti, and Ari Harju
    This file is part of GPUMD.
    GPUMD is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    GPUMD is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with GPUMD.  If not, see <http://www.gnu.org/licenses/>.
*/

/*----------------------------------------------------------------------------80
Usage:
    Compile:
        g++ -O3 main.cpp nep.cpp
    run:
        ./a.out
------------------------------------------------------------------------------*/

#include "nep.h"
#include "utility.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <time.h>

const int MN = 1000;
const int num_repeats = 1;

struct Atom {
  int N;
  std::vector<int> num_cells, type, NN_radial, NL_radial, NN_angular, NL_angular;
  std::vector<double> box, ebox, position, r12, potential, force, virial;
};
void readXYZ(Atom& atom);
void timing(Atom& atom, NEP3& nep3);
void compare_analytical_and_finite_difference(Atom& atom, NEP3& nep3);
void get_descriptor(Atom& atom, NEP3& nep3);

int main(int argc, char* argv[])
{
  Atom atom;
  readXYZ(atom);
  NEP3 nep3(atom.N, "nep.txt");

  timing(atom, nep3);
  compare_analytical_and_finite_difference(atom, nep3);
  get_descriptor(atom, nep3);

  return 0;
}

void readXYZ(Atom& atom)
{
  std::cout << "Reading xyz.in.\n";

  std::ifstream input_file("xyz.in");

  if (!input_file) {
    std::cout << "Cannot open xyz.in\n";
    exit(1);
  }

  input_file >> atom.N;
  std::cout << "    Number of atoms is " << atom.N << ".\n";

  atom.num_cells.resize(3);
  atom.box.resize(18);
  atom.ebox.resize(18);
  input_file >> atom.box[0];
  input_file >> atom.box[3];
  input_file >> atom.box[6];
  input_file >> atom.box[1];
  input_file >> atom.box[4];
  input_file >> atom.box[7];
  input_file >> atom.box[2];
  input_file >> atom.box[5];
  input_file >> atom.box[8];
  get_inverse(atom.box.data());

  std::cout << "    Box matrix h = [a, b, c] is\n";
  for (int d1 = 0; d1 < 3; ++d1) {
    for (int d2 = 0; d2 < 3; ++d2) {
      std::cout << "\t" << atom.box[d1 * 3 + d2];
    }
    std::cout << "\n";
  }

  std::cout << "    Inverse box matrix g = inv(h) is\n";
  for (int d1 = 0; d1 < 3; ++d1) {
    for (int d2 = 0; d2 < 3; ++d2) {
      std::cout << "\t" << atom.box[9 + d1 * 3 + d2];
    }
    std::cout << "\n";
  }

  std::vector<std::string> atom_symbols = get_atom_symbols();

  atom.type.resize(atom.N);
  atom.NN_radial.resize(atom.N);
  atom.NL_radial.resize(atom.N * MN);
  atom.NN_angular.resize(atom.N);
  atom.NL_angular.resize(atom.N * MN);
  atom.r12.resize(atom.N * MN * 6);
  atom.position.resize(atom.N * 3);
  atom.potential.resize(atom.N);
  atom.force.resize(atom.N * 3);
  atom.virial.resize(atom.N * 9);

  for (int n = 0; n < atom.N; n++) {
    std::string atom_symbol_tmp;
    input_file >> atom_symbol_tmp >> atom.position[n] >> atom.position[n + atom.N] >>
      atom.position[n + atom.N * 2];
    bool is_allowed_element = false;
    for (int t = 0; t < atom_symbols.size(); ++t) {
      if (atom_symbol_tmp == atom_symbols[t]) {
        atom.type[n] = t;
        is_allowed_element = true;
      }
    }
    if (!is_allowed_element) {
      std::cout << "There is atom in xyz.in that is not allowed in the used NEP potential.\n";
      exit(1);
    }
  }
  std::cout << "    Positions is\n";
  for (int i = 0; i < atom.position.size(); ++i) {
      std::cout << "\n" << atom.position[i];
    }
}

void timing(Atom& atom, NEP3& nep3)
{
  std::cout << "Started timing.\n";

  clock_t time_begin = clock();

  for (int n = 0; n < num_repeats; ++n) {
    find_neighbor_list_small_box(
      nep3.paramb.rc_radial, nep3.paramb.rc_angular, atom.N, atom.box, atom.position,
      atom.num_cells, atom.ebox, atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular,
      atom.r12);

    nep3.compute(
      atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular, atom.type, atom.r12,
      atom.potential, atom.force, atom.virial);
  }

  clock_t time_finish = clock();
  double time_used = (time_finish - time_begin) / double(CLOCKS_PER_SEC);
  std::cout << "    Number of atoms = " << atom.N << ".\n";
  std::cout << "    Number of steps = " << num_repeats << ".\n";
  std::cout << "    Time used = " << time_used << " s.\n";

  double speed = atom.N * num_repeats / time_used;
  double cost = 1000 / speed;
  std::cout << "    Computational speed = " << speed << " atom-step/second.\n";
  std::cout << "    Computational cost = " << cost << " mini-second/atom-step.\n";
}

void compare_analytical_and_finite_difference(Atom& atom, NEP3& nep3)
{
  std::cout << "Started validating force.\n";

  std::vector<double> force_finite_difference(atom.force.size());
  std::vector<double> position_copy(atom.position.size());
  for (int n = 0; n < atom.position.size(); ++n) {
    position_copy[n] = atom.position[n];
  }

  const double delta = 2.0e-5;

  for (int n = 0; n < atom.N; ++n) {
    for (int d = 0; d < 3; ++d) {
      atom.position[n + d * atom.N] = position_copy[n + d * atom.N] - delta; // negative shift

      find_neighbor_list_small_box(
        nep3.paramb.rc_radial, nep3.paramb.rc_angular, atom.N, atom.box, atom.position,
        atom.num_cells, atom.ebox, atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular,
        atom.r12);

      nep3.compute(
        atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular, atom.type, atom.r12,
        atom.potential, atom.force, atom.virial);

      double energy_negative_shift = 0.0;
      for (int n = 0; n < atom.N; ++n) {
        energy_negative_shift += atom.potential[n];
      }

      atom.position[n + d * atom.N] = position_copy[n + d * atom.N] + delta; // positive shift

      find_neighbor_list_small_box(
        nep3.paramb.rc_radial, nep3.paramb.rc_angular, atom.N, atom.box, atom.position,
        atom.num_cells, atom.ebox, atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular,
        atom.r12);

      nep3.compute(
        atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular, atom.type, atom.r12,
        atom.potential, atom.force, atom.virial);

      double energy_positive_shift = 0.0;
      for (int n = 0; n < atom.N; ++n) {
        energy_positive_shift += atom.potential[n];
      }

      force_finite_difference[n + d * atom.N] =
        (energy_negative_shift - energy_positive_shift) / (2.0 * delta);

      atom.position[n + d * atom.N] = position_copy[n + d * atom.N]; // back to original position
    }
  }

  find_neighbor_list_small_box(
    nep3.paramb.rc_radial, nep3.paramb.rc_angular, atom.N, atom.box, atom.position, atom.num_cells,
    atom.ebox, atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular, atom.r12);

  nep3.compute(
    atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular, atom.type, atom.r12,
    atom.potential, atom.force, atom.virial);

  std::ofstream output_file("force_analytical.out");

  if (!output_file.is_open()) {
    std::cout << "Cannot open force_analytical.out\n";
    exit(1);
  }
  output_file << std::setprecision(15);
  for (int n = 0; n < atom.N; ++n) {
    output_file << atom.force[n] << " " << atom.force[n + atom.N] << " "
                << atom.force[n + atom.N * 2] << "\n";
  }
  output_file.close();
  std::cout << "    analytical forces are written into force_analytical.out.\n";

  std::ofstream output_finite_difference("force_finite_difference.out");

  if (!output_finite_difference.is_open()) {
    std::cout << "Cannot open force_finite_difference.out\n";
    exit(1);
  }
  output_finite_difference << std::setprecision(15);
  for (int n = 0; n < atom.N; ++n) {
    output_finite_difference << force_finite_difference[n] << " "
                             << force_finite_difference[n + atom.N] << " "
                             << force_finite_difference[n + atom.N * 2] << "\n";
  }
  output_finite_difference.close();
  std::cout << "    finite-difference forces are written into force_finite_difference.out.\n";
}

void get_descriptor(Atom& atom, NEP3& nep3)
{
  std::cout << "Getting descriptor.\n";

  std::vector<double> descriptor(nep3.Fp.size());

  find_neighbor_list_small_box(
    nep3.paramb.rc_radial, nep3.paramb.rc_angular, atom.N, atom.box, atom.position, atom.num_cells,
    atom.ebox, atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular, atom.r12);

  nep3.find_descriptor(
    atom.NN_radial, atom.NL_radial, atom.NN_angular, atom.NL_angular, atom.type, atom.r12,
    descriptor);

  std::ofstream output_file("descriptor.out");

  if (!output_file.is_open()) {
    std::cout << "Cannot open descriptor.out\n";
    exit(1);
  }

  output_file << std::setprecision(15);

  for (int n = 0; n < atom.N; ++n) {
    for (int d = 0; d < nep3.annmb.dim; ++d) {
      output_file << descriptor[d * atom.N + n] << " ";
    }
    output_file << "\n";
  }
  output_file.close();
  std::cout << "    descriptors are written into descriptor.out.\n";
}