# OpenMM GaussVol plugin

A plugin that implements the Gaussian molecular volume and surface area model[1,2,3] in OpenMM.

Support for this model continues as part of the [AGBNP OpenMM plugin](https://github.com/egallicc/openmm_agbnp_plugin). Please refer to the AGBNP project for production work. 

Emilio Gallicchio <egallicchio@brooklyn.cuny.edu>
Last Modified: January 2017


## License

This software is released under the LGPL license. See LICENSE.

## Credits

This software is developed and maintained by Emilio Gallicchio <egallicchio@brooklyn.cuny.edu> with support from a grant from the National Science Foundation (ACI 1440665).

The plugin interface is based on the [openmmexampleplugin](https://github.com/peastman/openmmexampleplugin) by Peter Eastman.

## Installation

These instructions assume Linux. Install OpenMM 7; the easiest is through `miniconda` using [these instructions](https://simtk.org/frs/download_start.php/file/4907/Conda%20installation%20instruction?group_id=161). Install `swig` through `conda` as well:

```
conda install -c omnia openmm swig
```

Locate the OpenMM installation directory, otherwise it will default to `/usr/local/openmm`. If OpenMM was installed via `conda` the OpenMM installation directory will be something like `$HOME/miniconda2/pkgs/openmm-7.0.1-py27_0`

Download this plugin package from github:

```
git clone https://github.com/egallicc/openmm_gaussvol_plugin.git
```

Build and install the plugin with cmake. For example, assuming a unix system and a `conda` environment:
```
. ~/miniconda2/bin/activate
mkdir build_openmm_gaussvol_plugin
cd build_openmm_gaussvol_plugin
ccmake -i ../openmm_gaussvol_plugin
```

Hit `c` (configure) until all variables are correctly set, then `g` to generate the makefiles. `OPENMM_DIR` should point to the OpenMM installation directory. `CMAKE_INSTALL_PREFIX` normally is the same as `OPENMM_DIR`. The plugin requires the python API. You need `python` and `swig` to install it.

Once the configuration is done do:

```
make
make install
make PythonInstall
```

The last two steps may need superuser access depending on the installation target, or use the recommended `conda` environment.

## Test

`cd` to the directory where you cloned the `openmm_gaussvol_plugin` sources. Then:

```
cd example
export OPENMM_PLUGIN_DIR=<openmm_dir>/lib/plugins
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:<openmm_dir>/lib:<openmm_dir>lib/plugins
python test_gaussvol.py
```

where `<openmm_dir>` is the OpenMM installation directory.

## External Reference
The installation instructions for gaussVol using miniconda, openMM and Python2 (or Python3) can also be found in at  https://combiorecipe.wordpress.com.

## API

The plugin implements an energy function of the form E=γA where γ is the surface tension parameter and A is the solute surface area. It is added to OpenMM's force stack and configured using standard OpenMM API calls (see `TestReferenceGVolForce.cpp` in the `test` directory of each platform and `desmonddmsfile.py` in `example` for specific examples).

Schematically in C++:
```
#include "openmm/GVolForce.h"
...
GVolForce* force = new GVolForce();
system.addForce(force);
```

and in Python:

```
from GVolplugin import GVolForce
gv = GVolForce()
...
sys.addForce(gv)
```

The force is configured by assigning to each atom a radius, a surface tension parameter, and a boolean indicating whether the atom is a hydrogen atom:

```
double radius, gamma;
bool ishydrogen;
...
force->addParticle(radius, gamma, ishydrogen);
```

or, in Python,

```
gv.addParticle(radiusN, gammaN, h_flag)
```

Again, consult the `TestReferenceGVolForce.cpp` and `desmonddmsfile.py` for detailed examples.

The radius is given in nm, and the surface tension parameter is in kj/mol/nm^2. While the API allows setting individual surface tension parameters to each atom, GaussVol currently returns correct gradients only when all of the atoms have the same surface tension parameter. This behavior is consistent with a model of cavity formation in the solvent (see reference 3).

## Relevant references:

1. Baofeng Zhang, Denise Kilburg, Peter Eastman, Vijay S. Pande, Emilio Gallicchio. Efficient Gaussian Density Formulation of Volume and Surface Areas of Macromolecules on Graphical Processing Units. J. Comp. Chem 38, 740-752 (2017). [pdf of submitted manuscript](http://www.compmolbiophysbc.org/publications)
2. Grant, J. A., and B. T. Pickup. "A Gaussian description of molecular shape." The Journal of Physical Chemistry 99.11 (1995): 3503-3510.
3. Gallicchio, Emilio, Kristina Paris, and Ronald M. Levy. "The AGBNP2 implicit solvation model." Journal of chemical theory and computation 5.9 (2009): 2544-2564.

