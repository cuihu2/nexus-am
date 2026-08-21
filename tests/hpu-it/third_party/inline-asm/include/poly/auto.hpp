#pragma once

#include <string>

std::string generate_hpu_auto_body_asm(
	int N,
	int num_q,
	int num_p,
	int dnum,
	int auto_idx,
	bool append_psync = false);

std::string generate_hpu_auto_asm(
	int N,
	int num_q,
	int num_p,
	int dnum,
	int auto_idx,
	bool append_psync = true);
