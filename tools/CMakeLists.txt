# WTHOR training pipeline. Opt-in (REVERSI_BUILD_TOOLS, default OFF) - not part of the
# routine developer/CI build; only needed when actually regenerating pattern-eval weights.
add_subdirectory(wthor_extractor)

# Multi-ProbCut (M7) data generation + coefficient fitting. Same opt-in gate; only needed when
# regenerating the MPC model.
add_subdirectory(mpc_fitter)
