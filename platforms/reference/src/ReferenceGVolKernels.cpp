/* -------------------------------------------------------------------------- *
 *                               OpenMM-GVol                                *
 * -------------------------------------------------------------------------- */

#include <iostream>
#include <cstdlib>
#include <cmath>
#include "ReferenceGVolKernels.h"
#include "GVolForce.h"
#include "openmm/OpenMMException.h"
#include "openmm/internal/ContextImpl.h"
#include "openmm/reference/RealVec.h"
#include "openmm/reference/ReferencePlatform.h"
#include "gaussvol.h"


using namespace GVolPlugin;
using namespace OpenMM;
using namespace std;

static vector<RealVec>& extractPositions(ContextImpl& context) {
    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    return *((vector<RealVec>*) data->positions);
}

static vector<RealVec>& extractForces(ContextImpl& context) {
    ReferencePlatform::PlatformData* data = reinterpret_cast<ReferencePlatform::PlatformData*>(context.getPlatformData());
    return *((vector<RealVec>*) data->forces);
}

// Initializes GVol library
void ReferenceCalcGVolForceKernel::initialize(const System& system, const GVolForce& force) {
    
    numParticles = force.getNumParticles();

    //input lists
    positions.resize(numParticles);
    radii.resize(numParticles);
    gammas.resize(numParticles);
    ishydrogen.resize(numParticles);

    //output lists
    free_volume.resize(numParticles);
    self_volume.resize(numParticles);
    surface_areas.resize(numParticles);
    surf_force.resize(numParticles);
    
    double ang2nm = 0.1;
    for (int i = 0; i < numParticles; i++){
      double r, g;
      bool h;
      force.getParticleParameters(i, r, g, h);
      radii[i] = r;
      gammas[i] = g;
      ishydrogen[i] = h;
    }

    //create and saves GVol instance
    gvol = new GaussVol(numParticles, radii, gammas, ishydrogen);
}

double ReferenceCalcGVolForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    vector<RealVec>& pos = extractPositions(context);
    vector<RealVec>& force = extractForces(context);
    bool verbose = false;
    int init = 0;

    //for (int i = 0; i < numParticles; i++){
    //  positions[i] = pos[i];
    //}

    // Compute the interactions.
    gvol->enerforc(init, pos, surf_energy, surf_force, free_volume, self_volume, surface_areas);

    //returns the gradients
    for(int i = 0; i < numParticles; i++){
      force[i] += surf_force[i];
    }

#ifdef NOTNOW
    vector<int> nov, nov_2body;
    gvol->getstat(nov, nov_2body);    
    int nn = 0, nn2 = 0;
    for(int i = 0; i < nov.size(); i++){
      nn += nov[i];
     nn2 += nov_2body[i];
    }
    cout << "Noverlaps: " << nn << " " << nn2 << endl;
#endif

    if(verbose){
      //to input to freesasa
      double nm2ang = 10.0;
      cout << "id x y z radius: (Ang)" << endl;
      for(int i = 0; i < numParticles; i++){
	double rad = radii[i];
	if(ishydrogen[i] == 1) rad = 0.0;
	cout << "xyzr: " << i << " " << nm2ang*pos[i][0] << " " << nm2ang*pos[i][1] << " " << nm2ang*pos[i][2] << " " << nm2ang*rad << endl;
      }
    }


    if(verbose){
      cout << "Surface areas:" << endl;
      double tot_surf_area = 0.0;
      for(int i = 0; i < numParticles; i++){
	cout << i << " " << surface_areas[i] << endl;
	tot_surf_area += surface_areas[i];
      }
      cout << "Total surface area: (Ang^2) " << 100.0*tot_surf_area << endl;
      cout << "Energy: " << surf_energy << endl;
    }

    //returns energy
    return (double)surf_energy;
}

void ReferenceCalcGVolForceKernel::copyParametersToContext(ContextImpl& context, const GVolForce& force) {
  std::vector<int> neighbors;
    if (force.getNumParticles() != numParticles)
        throw OpenMMException("updateParametersInContext: The number of GVol particles has changed");
    for (int i = 0; i < force.getNumParticles(); i++) {
      double r, g;
      bool h;
      force.getParticleParameters(i, r, g, h);
    }
}
