#include <cmath>
#include <cfloat>
#include <vector>
#include <iostream>
#include <algorithm>
#include "openmm/reference/SimTKOpenMMRealType.h"
#include "openmm/reference/RealVec.h"
#include "gaussvol.h"

using namespace std;

#define PI (3.14159265359)
/* conversion factors from spheres to Gaussians */
#define KFC (2.2269859253)
#define PFC (2.5);

// have switching function
#define MIN_GVOL (FLT_MIN)

//suitable for w/o switching function
//#define MIN_GVOL (1.e-2)

// maximum overlap level
#define MAX_ORDER (12)

//counts overlaps
static int _nov_ = 0; 

//use nm and kj
#define ANG (0.1f)
#define ANG3 (0.001f)

//volume cutoffs in switching function
#define VOLMINA (0.01f*ANG3)
#define VOLMINB (0.1f*ANG3)

//radius offset for surf energy calc.
#define SA_DR (0.1f*ANG)

/* overlap volume switching function + 1st derivative */
static RealOpenMM pol_switchfunc(RealOpenMM gvol, RealOpenMM volmina, RealOpenMM volminb, RealOpenMM &sp){

  RealOpenMM swf = 0.0f;
  RealOpenMM swfp = 1.0f;
  RealOpenMM swd, swu, swu2, swu3, s;
  if(gvol > volminb) {
    swf = 1.0f;
    swfp = 0.0f;
  }else if(gvol < volmina){
    swf = 0.0f;
    swfp = 0.0f;
  }
  swd = 1.f/(volminb - volmina);
  swu = (gvol - volmina)*swd;
  swu2 = swu*swu;
  swu3 = swu*swu2;
  s = swf + swfp*swu3*(10.f-15.f*swu+6.f*swu2);
  sp = swfp*swd*30.f*swu2*(1.f - 2.f*swu + swu2);

  //turn off switching function
  //*sp = 0.0;
  //s = 1.0;
  return s;
}

/* overlap between two Gaussians represented by a (V,c,a) triplet
   V: volume of Gaussian
   c: position of Gaussian
   a: exponential coefficient

   g(x) = V (a/pi)^(3/2) exp(-a(x-c)^2)

   this version is based on V=V(V1,V2,r1,r2,alpha)
   alpha = (a1 + a2)/(a1 a2)

   dVdr is (1/r)*(dV12/dr)
   dVdV is dV12/dV1 
   dVdalpha is dV12/dalpha
   d2Vdalphadr is (1/r)*d^2V12/dalpha dr
   d2VdVdr is (1/r) d^2V12/dV1 dr

*/
static RealOpenMM ogauss_alpha(GaussianVca &g1, GaussianVca &g2, GaussianVca &g12, RealOpenMM &dVdr, RealOpenMM &dVdV, RealOpenMM &sfp){
  RealOpenMM d2, dx, dy, dz;
  RealVec c1 = g1.c;
  RealVec c2 = g2.c;
  RealVec dist;
  RealOpenMM deltai, gvol, p12, a12;
  RealOpenMM s, sp, df, dgvol, dgvolv, ef, dgvola2, dgvola1, dgalpha, dgalpha2, dgvolvdr;

  dist = c2 - c1;
  d2 = dist.dot(dist);

  a12 = g1.a + g2.a;
  deltai = 1./a12;
  df = (g1.a)*(g2.a)*deltai; // 1/alpha

  ef = exp(-df*d2);
  gvol = ( (g1.v * g2.v)/pow(PI/df,1.5))*ef;
  dgvol = -2.f*df*gvol; // (1/r)*(dV/dr) w/o switching function
  dgvolv = gvol/g1.v;     // (dV/dV1)  w/o switching function

  /* parameters for overlap gaussian. Note that c1 and c2 are Vec3's and the "*" operator wants 
     the vector first and scalar second vector2 = vector1 * scalar */
  g12.c = ((c1 * g1.a) + (c2 * g2.a)) * deltai;
  g12.a = a12;
  g12.v = gvol;

  /* switching function */
  s = pol_switchfunc(gvol, VOLMINA, VOLMINB, sp);
  sfp = sp*gvol+s;
  dVdr = dgvol;
  dVdV = dgvolv;

  return s*gvol;
}

/* overlap comparison function */
static bool goverlap_compare( const GOverlap &overlap1, const GOverlap &overlap2) {
  /* order by volume, larger first */
  return overlap1.volume > overlap2.volume;
}

static int init_overlap_tree(GOverlap_Tree &tree,
			     int natoms, vector<RealVec> &pos,
			     vector<RealOpenMM> &radius, vector<RealOpenMM> &gamma,
			     vector<bool>ishydrogen){
  GOverlap overlap;
  
  // reset tree
  tree.overlaps.clear();

  /* slot 0 contains the master tree information, children = all of the atoms */
  overlap.level = 0;
  overlap.volume = 0;
  overlap.dv1 = RealVec(0,0,0);
  overlap.dvv1 = 0.;
  overlap.self_volume = 0;
  overlap.sfp = 1.;
  overlap.gamma1i = 0.;
  overlap.parent_index = -1;
  overlap.atom = -1;
  overlap.children_startindex = 1;
  overlap.children_count = natoms;

  tree.overlaps.push_back(overlap);

  /* list of atoms start at slot #1 */
  for(int iat=0; iat<natoms; iat++){
    RealOpenMM a = KFC/(radius[iat]*radius[iat]);
    RealOpenMM volume = ishydrogen[iat] ? 0.0 : 4.*PI*pow(radius[iat],3)/3.;
    overlap.level = 1;
    overlap.g.v = volume;
    overlap.g.a = a;
    overlap.g.c = pos[iat];
    overlap.volume = volume;
    overlap.dv1 = RealVec(0,0,0);
    overlap.dvv1 = 1.; //dVi/dVi
    overlap.self_volume = 0.;
    overlap.sfp = 1.;
    overlap.gamma1i = gamma[iat]/SA_DR;
    overlap.parent_index = 0;
    overlap.atom = iat; 
    overlap.children_startindex = -1;
    overlap.children_count = -1;
    tree.overlaps.push_back(overlap);
  }


#ifdef TEST_SORT
  {
    for(int i=0;i<=natoms;i++){
      cout << "OV1: " << tree.overlaps[i].volume << endl;
    }
    sort(tree.overlaps.begin(), tree.overlaps.end(), goverlap_compare);
    for(int i=0;i<=natoms;i++){
      cout << "OV2: " << tree.overlaps[i].volume << endl;
    }
  }
#endif


  return 1;
}

/* adds to the tree the children of overlap identified by "parent_index" in the tree */
static int add_children(GOverlap_Tree &tree, int parent_index, vector<GOverlap> &children_overlaps){
  int i, ip, slot;

  /* adds children starting at the last slot */
  int start_index = tree.overlaps.size();
  
  int noverlaps = children_overlaps.size();

  /* retrieves address of root overlap */
  GOverlap *root = &(tree.overlaps[parent_index]);

  /* registers list of children */
  root->children_startindex = start_index;
  root->children_count = noverlaps;

  /* if 2-body sort neighbors by overlap volume */
  if(root->level == 1){
    sort(children_overlaps.begin(), children_overlaps.end(), goverlap_compare);
   }


  int root_level = root->level;

  /* now copies the children overlaps from temp buffer */
  for(int ip=0;ip<noverlaps;ip++){
    children_overlaps[ip].level = root_level + 1;
    // connect overlap to parent
    children_overlaps[ip].parent_index = parent_index;
    // reset its children indexes 
    children_overlaps[ip].children_startindex = -1;
    children_overlaps[ip].children_count = -1;
    // add to tree
    // note that the 'root' pointer may be invalidated by the push back below
    tree.overlaps.push_back(children_overlaps[ip]);
  }

  _nov_ += noverlaps;

  return start_index;
}


/* scans the siblings of overlap identified by "root_index" to create children overlaps,
   returns them into the "children_overlaps" buffer: (root) + (atom) -> (root, atom) */
static int compute_children(GOverlap_Tree &tree, int root_index, vector<GOverlap> &children_overlaps){
  int parent_index;
  int sibling_start, sibling_count;
  int j;

  /* reset output buffer */
  children_overlaps.clear();

  /* retrieves overlap */
  GOverlap &root = tree.overlaps[root_index];
  
  /* retrieves parent overlap */
  parent_index = root.parent_index;
  if(parent_index < 0) return 1;//master root? can't do compute_children() on master root
  if(root.level >= MAX_ORDER) return 1;
  GOverlap &parent = tree.overlaps[parent_index];

  /* retrieves start index and count of siblings */
  sibling_start = parent.children_startindex;
  sibling_count = parent.children_count;
  if(sibling_start < 0 || sibling_count < 0) return -1; //parent is not initialized?
  if(root_index < sibling_start && root_index > sibling_start + sibling_count -1 ) return -1; //this overlap somehow is not the child of registered parent

  /* now loops over "younger" siblings (i<j loop) to compute new overlaps */
  for(int slotj=root_index+1; slotj<sibling_start + sibling_count; slotj++){
    GaussianVca g12;
    GOverlap &sibling = tree.overlaps[slotj];
    RealOpenMM gvol, dVdr,dVdV, sfp;

    /* atomic gaussian of last atom of sibling */
    int atom2 = sibling.atom;
    GaussianVca &g1 = root.g;
    GaussianVca &g2 = tree.overlaps[atom2+1].g; //atoms are stored in the tree at indexes 1...N
    gvol = ogauss_alpha(g1, g2, g12, dVdr, dVdV, sfp);

    /* create child if overlap volume is not zero */
    if(gvol > MIN_GVOL){
      GOverlap ov;
      ov.g = g12;
      ov.volume = gvol;
      ov.self_volume = 0;
      ov.atom = atom2;
      // dv1 is the gradient of V(123..)n with respect to the position of 1
      ov.dv1 = ( g2.c - g1.c ) * (-dVdr);
      //dvv1 is the derivative of V(123...)n with respect to V(123...)
      ov.dvv1 = dVdV;
      ov.sfp = sfp;
      ov.gamma1i = root.gamma1i + tree.overlaps[atom2+1].gamma1i;
      children_overlaps.push_back(ov);
    }
  }
  
  return 1;
}


/*rescan the sub-tree to recompute the volumes, does not modify the tree */
static int rescan_r(GOverlap_Tree &tree, int slot){
  int parent_index;
  int sibling_start, sibling_count;

  /* this overlap  */
  GOverlap &ov = tree.overlaps[slot];

  /* recompute its own overlap by merging parent and last atom */
  parent_index = ov.parent_index;
  if(parent_index > 0){
    GaussianVca g12;
    RealOpenMM dVdr,dVdV, dVdalpha, d2Vdalphadr, d2VdVdr, sfp;
    int atom = ov.atom;
    GOverlap &parent  = tree.overlaps[parent_index];
    GaussianVca &g1 = parent.g;
    GaussianVca &g2 = tree.overlaps[atom+1].g; //atoms are stored in the tree at indexes 1...N
    RealOpenMM gvol = ogauss_alpha(g1,g2, g12,dVdr,dVdV,sfp);
    ov.g = g12;
    ov.volume = gvol;
    // dv1 is the gradient of V(123..)n with respect to the position of 1
    ov.dv1 = ( g2.c - g1.c ) * (-dVdr);
    //dvv1 is the derivative of V(123...)n with respect to V(123...)
    ov.dvv1 = dVdV;
    ov.sfp = sfp;
    ov.gamma1i = parent.gamma1i + tree.overlaps[atom+1].gamma1i;
  }
  
  /* calls itself recursively on the children */
  for(int slot_child=ov.children_startindex ; slot_child < ov.children_startindex+ov.children_count ; slot_child++){
    rescan_r(tree, slot_child);
  }

  return 1;
}

/*rescan the tree to recompute the volumes, does not modify the tree */
static int rescan_tree(GOverlap_Tree &tree, 
		       int natoms, vector<RealVec> &pos, vector<RealOpenMM> &radius, vector<RealOpenMM> &gamma,
		       vector<bool>ishydrogen){

  int slot;
  
  slot = 0;
  GOverlap *ov = &(tree.overlaps[slot]);
  ov->level = 0;
  ov->volume = 0;
  ov->dv1 = RealVec(0,0,0);
  ov->dvv1 = 0.;
  ov->self_volume = 0;
  ov->sfp = 1.;
  ov->gamma1i = 0.;

  slot = 1;
  for(int iat=0;iat<natoms;iat++, slot++){
    RealOpenMM a = KFC/(radius[iat]*radius[iat]);
    RealOpenMM volume = ishydrogen[iat] ? 0.0 : 4.*PI*pow(radius[iat],3)/3.;
    ov = &(tree.overlaps[slot]);
    ov->level = 1;
    ov->g.v = volume;
    ov->g.a = a;
    ov->g.c = pos[iat];
    ov->volume = volume;
    ov->dv1 = RealVec(0,0,0);
    ov->dvv1 = 1.; //dVi/dVi
    ov->self_volume = 0.;
    ov->sfp = 1.;
    ov->gamma1i = gamma[iat]/SA_DR;
   }

  rescan_r(tree,0);
  return 1;
}


static int compute_andadd_children_r(GOverlap_Tree &tree, int root){
  vector<GOverlap> children_overlaps;
  compute_children(tree, root, children_overlaps);
  int noverlaps = children_overlaps.size();
  if(noverlaps>0){
    int start_slot = add_children(tree, root, children_overlaps);
    for (int ichild=start_slot; ichild < start_slot + noverlaps ; ichild++){
      compute_andadd_children_r(tree, ichild);
    }
  }
  return 1;
}

static int compute_overlap_tree_r(GOverlap_Tree &tree,
				  int natoms, vector<RealVec> &pos, vector<RealOpenMM> &radius, 
				  vector<RealOpenMM> &gamma, vector<bool> ishydrogen){
  init_overlap_tree(tree, natoms, pos, radius, gamma, ishydrogen);
  for(int slot = 1; slot <= natoms ; slot++){
    compute_andadd_children_r(tree, slot);
  }
  return 1;
}

/* compute volumes, energy of this volume and calls itself to get the volumes of the children */
static int compute_volume_underslot2_r
(
 GOverlap_Tree &tree, int slot,
 RealOpenMM &psi1i, RealOpenMM &f1i, RealVec &p1i, //subtree accumulators for free volume
 RealOpenMM &psip1i, RealOpenMM &fp1i, RealVec &pp1i, //subtree accumulators for self volume
 RealOpenMM &energy1i, RealOpenMM &fenergy1i, RealVec &penergy1i, //subtree accumulators for volume-based energy
 vector<RealVec>  &dv,          //gradients of volume-based energy				
 vector<RealOpenMM> &free_volume, //atomic free volumes
 vector<RealOpenMM> &self_volume  //atomic self volumes
){

  GOverlap &ov = tree.overlaps[slot];
  RealOpenMM cf = ov.level % 2 == 0 ? -1.0 : 1.0;
  RealOpenMM volcoeff  = ov.level > 0 ? cf : 0;
  RealOpenMM volcoeffp = ov.level > 0 ? volcoeff/(RealOpenMM)ov.level : 0;

  int atom = ov.atom;
  RealOpenMM ai = tree.overlaps[atom+1].g.a;
  RealOpenMM a1i = ov.g.a;
  RealOpenMM a1 = a1i - ai;
 
  int i,j;
  RealOpenMM c1, c1p, c2;

  psi1i = volcoeff*ov.volume; //for free volumes
  f1i = volcoeff*ov.sfp ;
  p1i = RealVec(0,0,0);

  psip1i = volcoeffp*ov.volume; //for self volumes
  fp1i = volcoeffp*ov.sfp;
  pp1i = RealVec(0,0,0);

  energy1i = volcoeffp*ov.gamma1i*ov.volume; //EV energy
  fenergy1i = volcoeffp*ov.sfp*ov.gamma1i;
  penergy1i = RealVec(0,0,0);


  if(ov.children_startindex >= 0){
    int sloti;
    for(int sloti=ov.children_startindex ; sloti < ov.children_startindex+ov.children_count ; sloti++){
      RealOpenMM psi1it, f1it; RealVec p1it;
      RealOpenMM psip1it, fp1it; RealVec pp1it;
      RealOpenMM energy1it, fenergy1it; RealVec penergy1it;
      compute_volume_underslot2_r(tree, sloti,
				  psi1it, f1it, p1it, 
				  psip1it, fp1it, pp1it,
				  energy1it, fenergy1it, penergy1it,
				  dv, free_volume, self_volume);

      psi1i += psi1it;
      f1i += f1it;
      p1i += p1it;

      psip1i += psip1it;
      fp1i += fp1it;
      pp1i += pp1it;

      energy1i += energy1it;
      fenergy1i += fenergy1it;
      penergy1i += penergy1it;

    }
  }

  if(ov.level > 0){
    //contributions to free and self volume of last atom
    free_volume[atom] += psi1i;
    self_volume[atom] += psip1i;

    //contributions to energy gradients
    c2 = ai/a1i;
    dv[atom] += (-ov.dv1) * fenergy1i + penergy1i * c2;

    //update subtree P1..i's for parent
    c2 = a1/a1i;
    p1i = (ov.dv1) * f1i + p1i * c2;
    pp1i = (ov.dv1) * fp1i + pp1i * c2;
    penergy1i = (ov.dv1) * fenergy1i + penergy1i * c2;
    //update subtree F1..i's for parent
    f1i = ov.dvv1 * f1i;
    fp1i = ov.dvv1 * fp1i;
    fenergy1i = ov.dvv1 * fenergy1i;
  }
  return 1;
}

/* traverses tree and computes volumes, etc. */
static int compute_volume2_r(GOverlap_Tree &tree, 
			     int natoms, vector<RealVec> &pos,
			     RealOpenMM &volume, RealOpenMM &energy, 
			     vector<RealVec> &dv, vector<RealOpenMM> &free_volume, vector<RealOpenMM> &self_volume){ 
  
  int slot = 0;
  int i,j;
  RealOpenMM psi1i, f1i; RealVec p1i; //subtree accumulators for (free) volume
  RealOpenMM psip1i, fp1i; RealVec pp1i; //subtree accumulators for self volume
  RealOpenMM energy1i, fenergy1i; RealVec penergy1i; //subtree accumulators for volume-based energy

  // reset volumes, gradients
  RealVec zero3 = RealVec(0,0,0);
  for(int i = 0 ; i < dv.size(); ++i) dv[i] = zero3;
  for(int i = 0 ; i < free_volume.size(); ++i) free_volume[i] = 0;
  for(int i = 0 ; i < self_volume.size(); ++i) self_volume[i] = 0;

  compute_volume_underslot2_r(tree, 0,
			      psi1i, f1i, p1i, 
			      psip1i, fp1i, pp1i,
			      energy1i, fenergy1i, penergy1i,
			      dv, free_volume, self_volume);
  
  volume = psi1i;
  energy = energy1i;
  return 1;
}


/* same as compute_andadd_children_r() but does not call itself on the children */
static int compute_andadd_children_seq(GOverlap_Tree &tree, int root){
  vector<GOverlap> children_overlaps;
  compute_children(tree, root, children_overlaps);
  int noverlaps = children_overlaps.size();
  if(noverlaps>0){
    int start_slot = add_children(tree, root, children_overlaps);
  }
  return 1;
}


/* same as compute_overlap_tree_r() but computes 2-body first, then completes recursively
   this mimics the GPU algorithm
 */
static int compute_overlap_tree_seq(GOverlap_Tree &tree,
				  int natoms, vector<RealVec> &pos, vector<RealOpenMM> &radius, 
				  vector<RealOpenMM> &gamma, vector<bool> ishydrogen){
  init_overlap_tree(tree, natoms, pos, radius, gamma, ishydrogen);
  // compute 2-body only
  for(int slot = 1; slot <= natoms ; slot++){
    compute_andadd_children_seq(tree, slot);
  }
  // now run recursively on the 3-body
  int end2body = 0;
  for(int slot = 1; slot <= tree.natoms; slot++){
    if(tree.overlaps[slot].children_startindex > 0) end2body = tree.overlaps[slot].children_startindex;
  }
  for(int slot = natoms+1; slot <= end2body ; slot++){
    compute_andadd_children_r(tree, slot);
  }
  
  return 1;
}


/* print overlaps up to 2-body */
static void print_flat_tree_2body(GOverlap_Tree &tree){
  int end = 0;
  // the end of the 2-body must be given by the last child of the last atom with children
  for(int slot = 1; slot <= tree.natoms; slot++){
    if(tree.overlaps[slot].children_startindex > 0) end = tree.overlaps[slot].children_startindex;
  }
  //now print
  for(int slot = 0; slot <= end ; slot++){
    GOverlap *ov = &(tree.overlaps[slot]);
    cout << slot << " " << ov->volume << " " << ov->children_startindex << " " << ov->children_count << endl;
  }
}

/*
 * End of sequential functions
 */


static void test_gaussian(GOverlap_Tree &tree){
  /* test ogauss derivatives */
  GaussianVca g1, g2, g12;
  int iter, niter = 1000;
  RealOpenMM d, dx, gvol, dVdr, dVdV, gvol_old, sfp;
  RealVec grad, dist;

  g1 = tree.overlaps[1].g;
  g2 = tree.overlaps[2].g;
  gvol = gvol_old = ogauss_alpha(g1, g2, g12, dVdr, dVdV, sfp);
  dist = g2.c - g1.c;
  grad = dist * (-dVdr) * sfp; //gradient with respect to position of g2
  d = sqrt(dist.dot(dist));
  dx = 0.01;
  for(iter = 0; iter < niter; iter++){
    g1.c[2] += dx;
    gvol = ogauss_alpha(g1, g2, g12, dVdr, dVdV, sfp);
    dist = g2.c - g1.c;
    grad = dist * (-dVdr) * sfp; //gradient with respect to position of g2
    d = sqrt(dist.dot(dist));
    cout << d << " " << gvol << " " << gvol-gvol_old << " " << grad[2]*dx << endl;
    gvol_old = gvol;
  }
}


 GaussVol::GaussVol(int natoms, vector<RealOpenMM> &radii, 
		    vector<RealOpenMM> &gammas, vector<bool>& ishydrogen_in){   
   bool verbose = false;
   tree.natoms = natoms;
   radius1 = radii;
   gamma = gammas;
   ishydrogen = ishydrogen_in;
   radius2.resize(natoms);
   for(int i = 0; i < natoms; ++i) radius2[i] = radius1[i] - SA_DR;
   grad1.resize(natoms);
   grad2.resize(natoms);
   self_volume2.resize(natoms);
   free_volume2.resize(natoms);   

   if(verbose){
     for(int i = 0; i<natoms; i++){
       cout << i << " " << radius1[i] << " " << gamma[i] << " " << radius2[i] << " " << ishydrogen[i] << endl;
     }
   }
   //cout << natoms << " " << radius1.size() << " " << gamma.size() << " " << ishydrogen.size() << endl;


 }


 void GaussVol::enerforc(const int init,
			 vector<RealVec> &positions,
			 RealOpenMM &energy,
			 vector<RealVec> &force,
			 vector<RealOpenMM> &free_volume,  vector<RealOpenMM> &self_volume,
			 vector<RealOpenMM> &surface_areas){
   RealOpenMM volume1, volume2, energy1, energy2;
   int natoms = tree.natoms;
   bool rescan = false;
   bool verbose = false;
   _nov_ = 0;

 
  if(verbose){
     for(int i = 0; i<natoms; i++){
       cout << i << " " << positions[i][0] << " " << positions[i][1] << " " << positions[i][2] << endl;
     }
   }

   compute_overlap_tree_r(tree, natoms, positions, radius1, gamma, ishydrogen);

   //print_flat_tree_2body(tree);
   
   compute_volume2_r(tree, 
   		     natoms, positions,
   		     volume1, energy1, 
   		     grad1, 
   		     free_volume, self_volume); 
   if(verbose){
     cout << "Self Volumes:" << endl;
     float mol_volume = 0;
     for(int i = 0; i < natoms; ++i) mol_volume += self_volume[i];
     for(int i = 0; i < natoms; ++i) cout << i << " " << self_volume[i] << endl;
     cout << "Mol Volume:" << mol_volume << endl;
   }

   if(rescan){
     rescan_tree(tree, natoms, positions, radius2, gamma, ishydrogen);
     compute_volume2_r(tree, 
		       natoms, positions,
		       volume2, energy2, 
		       grad2, 
		       free_volume2, self_volume2);
     energy = energy1 - energy2;
     if(verbose) cout << "E: " << energy1 << " " << energy2 << endl;
   }else{
     energy = energy1;
     if(verbose) cout << "E: " << energy1 << endl;
   }

   if(rescan){
     for(int i = 0; i < natoms; ++i) force[i] = -(grad1[i] - grad2[i]);
     for(int i = 0; i < natoms; ++i) surface_areas[i] = (self_volume[i] - self_volume2[i])/SA_DR; 
   }else{
     for(int i = 0; i < natoms; ++i) force[i] = -grad1[i];
   }
 }

// returns number of overlaps for each atom 
void GaussVol::getstat(vector<int>& nov, vector<int>& nov_2body){
   int natoms = tree.natoms;
   int tree_size = tree.overlaps.size();
   for(int slot = 0; slot < tree_size; slot++){
     int atom = tree.overlaps[slot].atom;
     if(atom >= 0 && atom < natoms){
       nov[atom] += 1;
       if(tree.overlaps[slot].level == 2){
   	 nov_2body[atom] += 1;
       }
     }
   }
}
