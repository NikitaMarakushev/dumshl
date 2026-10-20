#pragma once
#include "types.hpp"

void reset_signals_for_child();
void sigchld_handler(int);
void reap_finished_jobs();
void execute_pipeline(const Pipeline& pipeline);