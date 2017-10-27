/* -------------------------------------------------------------------------- *
 *                              OpenMM-GVol                                 *
 * -------------------------------------------------------------------------- */

/**
 * This tests the Reference implementation of GVolForce.
 */

#include "GVolForce.h"
#include "openmm/internal/AssertionUtilities.h"
#include "openmm/Context.h"
#include "openmm/Platform.h"
#include "openmm/System.h"
#include "openmm/VerletIntegrator.h"
#include <cmath>
#include <iostream>
#include <vector>

using namespace GVolPlugin;
using namespace OpenMM;
using namespace std;

extern "C" OPENMM_EXPORT void registerGVolReferenceKernelFactories();

void testForce() {
    System system;
    GVolForce* force = new GVolForce();
    system.addForce(force);
    //read from stdin
    int numParticles = 0;
    double id, x, y, z, radius, charge;
    double epsilon, sigma, bornr;
    double gamma;
    bool ishydrogen;
    vector<Vec3> positions;

    int ih;
    double ang2nm = 0.1;
    double kcalmol2kjmol = 4.184;
    numParticles = 0;
    string atom_string, pdb_atom_name, residue_name, chain_name, residue_id;
    while(std::cin >> atom_string >> id >> pdb_atom_name >> residue_name >> chain_name >> residue_id >> x >> y >> z >> charge >> radius){
      numParticles += 1;
      system.addParticle(1.0);
      positions.push_back(Vec3(x, y, z)*ang2nm);
      ishydrogen = (pdb_atom_name[0] == 'H');
      radius *= ang2nm;
      gamma = 1.0;
      force->addParticle(radius, gamma, ishydrogen);      
    }
    // Compute the forces and energy.
    VerletIntegrator integ(1.0);
    Platform& platform = Platform::getPlatformByName("Reference");
    //It doesnt look like the reference platform supports changing precision
    //map<string, string> properties;
    //properties["Precision"] = "single";
    Context context(system, integ, platform);
    context.setPositions(positions);
    State state = context.getState(State::Energy | State::Forces);

    double energy1 = state.getPotentialEnergy();
    std::cout << "Surface Area: " <<  energy1  << std::endl;
}

int main() {
  try {
    registerGVolReferenceKernelFactories();
    testForce();
	//        testChangingParameters();
  }
  catch(const std::exception& e) {
    std::cout << "exception: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
