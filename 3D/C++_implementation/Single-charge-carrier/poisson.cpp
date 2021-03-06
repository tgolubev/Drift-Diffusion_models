#include "poisson.h"
#include <iostream>

Poisson::Poisson(const Parameters &params)
{
    CV = (params.N_dos*params.dz*params.dz*q)/(epsilon_0*Vt);
    num_cell_x = params.num_cell_x;
    num_cell_y = params.num_cell_y;
    num_cell_z = params.num_cell_z;
    Nx = num_cell_x - 1;
    Ny = num_cell_y - 1;
    Nz = num_cell_z - 1;
    num_elements = params.num_elements;
    //we already have a V_matrix in main--> don't make 1 of them!
    //V_matrix = Eigen::Tensor<double, 3> (num_cell_x+1, num_cell_y+1, num_cell_z+1);    //useful for calculating currents at end of each Va
    //V_matrix.setZero();
    netcharge = Eigen::Tensor<double, 3> (num_cell_x, num_cell_y, num_cell_z);  //only contains elements which are included in matrix (exclude bottom BC's)
    netcharge.setZero();

    //these diags vectors are not needed
//    main_diag.resize(num_elements+1);
//    upper_diag.resize(num_elements+1);
//    lower_diag.resize(num_elements+1);
//    far_lower_diag.resize(num_elements+1);
//    far_upper_diag.resize(num_elements+1);
    rhs.resize(num_elements);

    V_bottomBC.resize(num_cell_x+1, num_cell_y+1);  //these are the full planes of BC's (include even the left boundaries)
    V_topBC.resize(num_cell_x+1, num_cell_y+1);

    //----------------------------------------------------------------------------------------------------------
    // //MUST FILL WITH THE VALUES OF epsilon!!  WILL NEED TO MODIFY THIS WHEN HAVE SPACE VARYING
    epsilon = Eigen::Tensor<double, 3> (num_cell_x+2, num_cell_y+2, num_cell_z+2);
    epsilon_avg_X = Eigen::Tensor<double, 3> (num_cell_x+2, num_cell_y+2, num_cell_z+2);
    epsilon_avg_Y = Eigen::Tensor<double, 3> (num_cell_x+2, num_cell_y+2, num_cell_z+2);
    epsilon_avg_Z = Eigen::Tensor<double, 3> (num_cell_x+2, num_cell_y+2, num_cell_z+2);
    epsilon.setConstant(params.eps_active);  //the  parameter is already the RELATIVE DIELECTRICE CONSTANT!


    //Compute averaged mobilities
    //==============================================
    //THIS ISN'T WORKING PROPERLY RIGHT NOW!!!!
    //==============================================
    /*
    for (int i = 1; i <= num_cell_x; i++) {
        for (int j = 1; j <= num_cell_y; j++) {
            for (int k = 1; k <= num_cell_z; k++) {
                epsilon_avg_X(i,j,k) = (epsilon(i,j,k) + epsilon(i,j+1,k) + epsilon(i,j,k+1) + epsilon(i,j+1,k+1))/4.;
                epsilon_avg_Y(i,j,k) = (epsilon(i,j,k) + epsilon(i+1,j,k) + epsilon(i,j,k+1) + epsilon(i+1,j,k+1))/4.;
                epsilon_avg_Z(i,j,k) = (epsilon(i,j,k) + epsilon(i+1,j,k) + epsilon(i,j+1,k) + epsilon(i+1,j+1,k))/4.;

                //add num_cell+2 values for i to account for extra bndry pt
                epsilon_avg_X(num_cell_x+1,j,k) = epsilon_avg_X(1,j,k);  //2 b/c the epsilon avg indices start from 2 (like Bernoullis)
                epsilon_avg_Y(num_cell_x+1,j,k) = epsilon_avg_Y(1,j,k);
                epsilon_avg_Z(num_cell_x+1,j,k) = epsilon_avg_Z(1,j,k);

                //add num_cell+2 values for j to account for extra bndry pt
                epsilon_avg_X(i,num_cell_y+1,k) = epsilon_avg_X(i,1,k);
                epsilon_avg_Y(i,num_cell_y+1,k) = epsilon_avg_Y(i,1,k);
                epsilon_avg_Z(i,num_cell_y+1,k) = epsilon_avg_Z(i,1,k);
            }
           //NOTE: use inside the device value for the num_cell+2 values b/c
           //these actually correspond to num_cell values, since with  Neuman
           //BC's, we use the value inside the device twice, when discretizing
           //at the boundary.
            epsilon_avg_X(i,j,num_cell_z+1) = epsilon(i,j,num_cell_z);        //assume the average epsilon at the bndry pt (top electrode) = same as epsilon just inside.
            epsilon_avg_Y(i,j,num_cell_z+1) = epsilon(i,j,num_cell_z);
            epsilon_avg_Z(i,j,num_cell_z+1) = epsilon(i,j,num_cell_z);
        }
    }
    */
    //Scale the epsilons for using in the matrix
    epsilon_avg_X = ((params.dz*params.dz)/(params.dx*params.dx))*epsilon/18.;  //JUST DO THIS FOR NOW, SINCE ALL EPSILONS ARE THE SAME((params.dz*params.dz)/(params.dx*params.dx))*epsilon_avg_X/18.;  //scale
    epsilon_avg_Y = ((params.dz*params.dz)/(params.dy*params.dy))*epsilon/18.; //((params.dz*params.dz)/(params.dy*params.dy))*epsilon_avg_Y/18.;  //to take into account possible dz dx dy difference, multipy by coefficient...
    epsilon_avg_Z = epsilon/18.; //epsilon_avg_Z/18.;

    //THESE ARE CORRECT, i checked



    //===========================================================

    //LATER NEED TO MAKE THE SCALLING ADJUST AUTOMATICALLY BASED ON INPUT PARAMETERS

    //===========================================================

    //----------------------------------------------------------------------------------------------------------

    //allocate memory for the sparse matrix and rhs vector (Eig object)
    sp_matrix.resize(num_elements, num_elements);
    VecXd_rhs.resize(num_elements);   //only num_elements, b/c filling from index 0 (necessary for the sparse solver)

    //setup the triplet list for sparse matrix
    triplet_list.resize(11*num_elements);   //approximate the size that need

} //constructor)

//-------------------------------------------------------
//Set BC's functions


void Poisson::set_V_topBC(const Parameters &params, double Va)
{
    for (int j = 0; j <= num_cell_y; j++) {
        for (int i = 0; i <= num_cell_x; i++) {
            V_topBC(i,j) = Va/Vt;
        }
    }
}

void Poisson::set_V_bottomBC(const Parameters &params, double Va)
{
    for (int j = 0; j <= num_cell_y; j++) {
        for (int i = 0; i <= num_cell_x; i++) {
            V_bottomBC(i,j) = 0;
        }
    }
}



void Poisson::setup_matrix()  //Note: this is on purpose different than the setup_eqn used for Continuity eqn's, b/c I need to setup matrix only once
{

    trp_cnt = 0;  //NEED TO START THE COUNT!
    set_lowest_diag();  //BREAKS HERE
    set_lower_diag_Xs();   //lower diag corresponding to X direction finite differences
    set_lower_diag_Y_PBCs(); //lower diag corresponding to Y periodic boundary conditions
    set_lower_diag_Ys();
    set_main_lower_diag();
    set_main_diag();
    set_main_upper_diag();  //corresponds to Z direction finite differences
    set_upper_diag_Ys();
    set_upper_diag_Y_PBCs();
    set_upper_diag_Xs();
    set_highest_diag();


    typedef Eigen::Triplet<double> Trp;

    //generate triplets for Eigen sparse matrix
    //setup the triplet list for sparse matrix

     sp_matrix.setFromTriplets(triplet_list.begin(), triplet_list.end());    //sp_matrix is our sparse matrix
}


//---------------Setup AV diagonals (Poisson solve)---------------------------------------------------------------
//X's left PBC
void Poisson::set_lowest_diag()
{

    int index = 1;
    int i = 1;     //since is PBC, this is always i = 1
    for (int j = 1; j <= Ny+1; j++) {
        for (int k = 1; k <= Nz; k++) {  //ONLY GOES TO Nz, b/c of Dirichlet BC's at top electrode (included in the matrix)..., all elements excep main diag need to be 0
            triplet_list[trp_cnt] = {index-1+(Nx)*(Nz+1)*(Ny+1), index-1, -epsilon_avg_X(i,j,k)};  //note: don't need +1, b/c c++ values correspond directly to the inside pts
            //just  fill directly!! the triplet list. DON'T NEED THE DIAG VECTORS AT ALL!
            //RECALL, THAT the sparse matrices are indexed from 0 --> that's why have the -1's
            trp_cnt++;
            index = index +1;
        }
        index = index + 1;  //to take care of Dirichlet BC's
    }
}

//X's
void Poisson::set_lower_diag_Xs()
{
    int index = 1;
    for (int i = 1; i <= Nx; i++) {
        for (int j = 1; j <= Ny+1; j++) {
            for (int k = 1; k <= Nz; k++) {// only to Nz b/c of Dirichlet BCs
                triplet_list[trp_cnt] = {index-1+(Nz+1)*(Ny+1), index-1, -epsilon_avg_X(i+1,j,k)};
                trp_cnt++;
                index = index +1;
            }
            index = index + 1;  //to take care of Dirichlet BC's
        }
    }
}


//Y's left PBCs
void Poisson::set_lower_diag_Y_PBCs()
{
    int index = 1;
    int j = 1;   //always 1 b/c are bndry elements
    for (int i = 1; i <= Nx+1; i++) {
        for (int k = 1; k <= Nz; k++) { // only to Nz b/c of Dirichlet BCs
            triplet_list[trp_cnt] = {index-1+(Nz+1)*(Ny), index-1, -epsilon_avg_Y(i,j,k)};
            trp_cnt++;
            index = index +1;
        }
        index = index + 1;  //to take care of Dirichlet BC's
        index = index + (Nz+1)*(Ny);  // to skip the 0's subblocks
    }

}


//Y's
void Poisson::set_lower_diag_Ys()
{
    int index = 1;
    for (int i = 1; i <= Nx+1; i++) {
        for (int j = 1; j <= Ny; j++) {
            for (int k = 1; k <= Nz; k++) {// only to Nz b/c of Dirichlet BCs
                triplet_list[trp_cnt] = {index-1+(Nz+1), index-1, -epsilon_avg_Y(i,j+1,k)};
                trp_cnt++;
                index = index +1;
            }
            index = index + 1;  //to take care of Dirichlet BC's
        }
        index = index + (Nz+1);  // to skip the 0's subblocks
    }
}



//main lower diag
void Poisson::set_main_lower_diag()
{
    int index = 1;
    for (int i = 1; i <= Nx+1; i++) {
        for (int j = 1; j <= Ny+1; j++) {
            for (int k = 1; k <= Nz-1; k++) {// only to Nz-1 b/c of Dirichlet BCs and b/c is lower diag
                triplet_list[trp_cnt] = {index, index-1, -epsilon_avg_Z(i,j,k+1)};
                trp_cnt++;
                index = index +1;
            }
            index = index + 1;  //to take care of 0 for Dirichlet BC
            index = index + 1;  //skip the corner elements which are zero
        }
    }

}


//main diag
void Poisson::set_main_diag()
{
    int index = 1;
    for (int i = 1; i <= Nx+1; i++) {
        for (int j = 1; j <= Ny+1; j++) {
            for (int k = 1; k <= Nz; k++) { // only to Nz b/c of Dirichlet BCs
                triplet_list[trp_cnt] = {index-1, index-1, epsilon_avg_X(i,j,k) + epsilon_avg_X(i+1,j,k) + epsilon_avg_Y(i,j,k) + epsilon_avg_Y(i,j+1,k) + epsilon_avg_Z(i,j,k) + epsilon_avg_Z(i,j,k+1)};
                trp_cnt++;
                index = index +1;
            }
            //add the Dirichlet BC's element --> in matrix just have a 1
            triplet_list[trp_cnt] = {index-1, index-1, 1};
            trp_cnt++;
            index = index + 1;
        }
    }
}


//main upper diag
void Poisson::set_main_upper_diag()
{
    int index = 1;  //note: unlike Matlab, can always start index at 1 here, b/c not using any spdiags fnc
    for (int i = 1; i <= Nx+1; i++) {
        for (int j = 1; j <= Ny+1; j++) {
            for (int k = 1; k <= Nz; k++) {
                triplet_list[trp_cnt] = {index-1, index, -epsilon_avg_Z(i,j,k+1)};
                trp_cnt++;
                index = index +1;
            }
            index = index + 1; //to skip the 0 corner elements
        }
    }
}

//Y's
void Poisson::set_upper_diag_Ys()
{
    int index = 1;
    for (int i = 1; i <= Nx+1; i++) {
        for (int j = 1; j <= Ny; j++) {
            for (int k = 1; k <= Nz; k++) { // only to Nz b/c of Dirichlet BCs
                triplet_list[trp_cnt] = {index-1, index-1+(Nz+1), -epsilon_avg_Y(i,j+1,k)};
                trp_cnt++;
                index = index +1;
            }
            index = index + 1;  //to take care of Dirichlet BC's
        }
        index = index + (Nz+1);  // to skip the 0's subblocks
    }
}


//Y right PBCs
void Poisson::set_upper_diag_Y_PBCs()
{
    int index = 1;
    int j = Ny+1;  //corresponds to right y boundary
    for (int i = 1; i <= Nx+1; i++) {
        for (int k = 1; k <= Nz; k++) { // only to Nz b/c of Dirichlet BCs
            triplet_list[trp_cnt] = {index-1, index-1+(Nz+1)*Ny, -epsilon_avg_Y(i,j,k)};
            trp_cnt++;
            index = index +1;
        }
        index = index + 1;  //to take care of Dirichlet BC's
        index = index + (Nz+1)*(Ny);  // to skip the 0's subblocks
    }
}


//X's
void Poisson::set_upper_diag_Xs()
{
    int index = 1;
    for (int i = 1; i <= Nx; i++) {
        for (int j = 1; j <= Ny+1; j++) {
            for (int k = 1; k <= Nz; k++) {// only to Nz b/c of Dirichlet BCs
                triplet_list[trp_cnt] = {index-1, index-1+(Nz+1)*(Ny+1), -epsilon_avg_X(i+1,j,k)};
                trp_cnt++;
                index = index +1;
            }
            index = index + 1;  //to take care of Dirichlet BC's
        }
    }
}

//far upper diag X right PBC's
void Poisson::set_highest_diag()
{
    int index = 1;
    int i = Nx+1;     //corresponds to right boundary
    for (int j = 1; j <= Ny+1; j++) {
        for (int k = 1; k <= Nz; k++) {  // only to Nz b/c of Dirichlet BCs
            triplet_list[trp_cnt] = {index-1, index-1+(Nx)*(Nz+1)*(Ny+1), -epsilon_avg_X(i,j,k)};
            trp_cnt++;
            index = index +1;
        }
        index = index + 1;  //to take care of Dirichlet BC's
    }
}

//---------------------------------------------------------------------------------------------------

void Poisson::set_rhs(const Eigen::Tensor<double, 3> &p)
{
    for (int i = 0; i < num_elements; i++)
        rhs[i] = CV*(p(i))/18.; //NOTE: later need to make this scaling automatically determined //Note: this uses full device
        //NOTE: p is now a tensor, so need to use () to access it's elements, NOT [].

    //add on BC's
    int index = 0;
    for (int i = 1; i <= Nx+1; i++) {  //num_cell +1 for i and j b/c of the PBC's/including boundary pt--> but are setting to 1 anyway..., so doesn't matter much
        for (int j = 1; j <= Ny+1; j++) {
            for (int k = 1; k <= Nz+1; k++) {
                if (k == 1) //bottom BC
                    rhs[index] += (epsilon(i,j,0)/18.)*V_bottomBC(i,j);  //Note: scaled here, b/c of scaling in the matrix

                if (k == Nz+1) //top BC
                    rhs[index] = V_topBC(i,j);
                index++;

            }
        }
    }

    //set up VectorXd Eigen vector object for sparse solver
    //THIS CAN BE REMOVED LATER, AND CAN DIRECTLY SET IT UP IN ABOVE LOOP
    for (int i = 0; i < num_elements; i++) {
        VecXd_rhs(i) = rhs[i];   //fill VectorXd  rhs of the equation
    }

}
