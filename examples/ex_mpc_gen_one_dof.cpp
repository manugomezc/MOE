#include <MOE/MOE.hpp>
#include <Mahi/Mpc.hpp>
#include <Mahi/Util.hpp>

using namespace casadi;
using namespace mahi::mpc;
using mahi::util::PI;

SX format_A(SX A_full,std::vector<int> dof){
    SX A_res = SX::zeros(dof.size(),dof.size());
    for (auto i = 0; i < dof.size(); i++){
        for (auto j = 0; j < dof.size(); j++){
            A_res(i,j) = A_full(dof[i],dof[j]);
        }
    }
    return A_res;
}

SX format_B(SX B_full,std::vector<int> dof){
    SX B_res = SX::zeros(dof.size(),1);
    for (auto i = 0; i < dof.size(); i++){
        B_res(i) = B_full(dof[i]);
    }
    return B_res;
}

int main(int argc, char* argv[])
{
    mahi::util::Options options("options.exe", "Simple Program Demonstrating Options");

    options.add_options()
        ("l,linear", "Generates linearized model.")
        ("d,dof",    "Which DOF is being tested.", cxxopts::value<std::vector<int>>())
        ("h,help",   "Prints help message.");
    
    auto result = options.parse(argc, argv);

    if (result.count("help")){
        std::cout << options.help() << std::endl;
        return 0;
    }

    bool linear = result.count("linear") > 0;
    std::vector<int> dof;
    if (result.count("dof")){
        dof = result["dof"].as<std::vector<int>>();
        sort(dof.begin(),dof.end());
    }
    else{
        LOG(mahi::util::Error) << "Must specify which DOF is being tested.";
        return -1;
    }

    SX x, x_dot, u;
    std::string model_name;

    model_name = "moe";

    SX T0 = SX::sym("T0");
    SX T1 = SX::sym("T1");
    SX T2 = SX::sym("T2");
    SX T3 = SX::sym("T3");


    moe::MoeDynamicModel moe_model;

    SXVector q_full = {moe_model.q0,
                       moe_model.q1,
                       moe_model.q2,
                       moe_model.q3,
                       moe_model.q0_dot,
                       moe_model.q1_dot,
                       moe_model.q2_dot,
                       moe_model.q3_dot};
    SXVector q_vec;
    SXVector qd_vec;

    SXVector u_full = {T0, T1, T2, T3};
    SX u_full_SX = SX::vertcat(u_full);
    SXVector u_vec;

    // add all positions
    for (auto i = 0; i < dof.size(); i++) {
        q_vec.push_back(q_full[dof[i]]);
        qd_vec.push_back(q_full[dof[i]+4]);
        u_vec.push_back(u_full[dof[i]]);
    }
    
    
    moe_model.set_user_params({7,  // forearm
                               4,  // counterweight position
                               30}); // shoulder rotation
    // state vector
    SXVector x_vec = q_vec;
    x_vec.insert(x_vec.end(), qd_vec.begin(), qd_vec.end());
    x = SX::vertcat(x_vec);


    SX q_dot = SX::vertcat(qd_vec);

    auto zero_variables_vec = q_full;
    for (int i = (dof.size()-1); i >= 0; i--) zero_variables_vec.erase(zero_variables_vec.begin()+dof[i]+4);
    for (int i = (dof.size()-1); i >= 0; i--) zero_variables_vec.erase(zero_variables_vec.begin()+dof[i]);

    casadi::SX zero_variables = vertcat(zero_variables_vec);

    auto G = moe_model.cas_get_G();
    auto V = moe_model.cas_get_V();
    auto Friction = moe_model.cas_get_Friction();
    auto B_full = u_full_SX - V - G - Friction;
    // std::cout << "here" << std::endl;
    auto A_full = moe_model.cas_get_effective_M();
    // std::cout << "here" << std::endl;
    auto A_eom = format_A(A_full,dof);
    // std::cout << "hereA" << std::endl;
    auto B_eom = format_B(B_full,dof);
    // std::cout << "hereB" << std::endl;
    SX q_d_dot_nonzero = solve(A_eom,B_eom);
    // std::cout << q_d_dot_nonzero << std::endl;
    std::cout << zero_variables << std::endl;
    SX q_d_dot;
    if (!zero_variables_vec.empty()){
        q_d_dot = substitute(q_d_dot_nonzero, zero_variables, std::vector<double>(8-dof.size()*2,0));
    }
    else {
        q_d_dot = q_d_dot_nonzero;
    }

    u = SX::vertcat(u_vec);
    
    x_dot = vertcat(q_dot,q_d_dot);

    std::cout << u << std::endl;
    std::cout << x_dot << std::endl;

    std::string dof_string = "";
    for (auto d : dof) dof_string += std::to_string(d);

    if (linear) model_name = "linear_" + model_name + "_j" + dof_string;
    
    // Bounds on state
    std::vector<double> x_min(x.size1(),-inf);
    std::vector<double> x_max(x.size1(), inf);

    // Bounds for control
    std::vector<double> u_min(u.size1(),-inf);
    std::vector<double> u_max(u.size1(), inf);

    // settings for multiple shooting constructions
    mahi::util::Time time_step  = mahi::util::milliseconds(linear ? 2 : 2);
    int num_shooting_nodes = 25;

    ModelParameters model_parameters(model_name, // name
                                     x.size1(),                // num_x
                                     u.size1(),                // num_u
                                     time_step,                // step_size
                                     num_shooting_nodes,       // num_shooting_nodes
                                     linear);                  // is_linear;                  

    // 
    ModelGenerator my_generator(model_parameters, x, x_dot, u);

    my_generator.create_model();
    my_generator.generate_c_code();
    my_generator.compile_model();

    std::cout << "Finished generating models" << std::endl;
    return 0;
}
