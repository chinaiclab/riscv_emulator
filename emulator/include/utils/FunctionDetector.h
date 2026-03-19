#ifndef FUNCTION_DETECTOR_H
#define FUNCTION_DETECTOR_H

#include <vector>
#include <map>
#include <string>
#include <cstdint>

// Function to detect functions in a RISC-V binary
std::map<uint32_t, std::string> detect_functions(const std::vector<uint8_t>& program);

// Function to extract functions from an ELF file
std::map<uint32_t, std::string> extract_functions_from_elf(const std::string& elf_filename);

// Function to save function profiles to a file
void save_function_profiles(const std::map<uint32_t, std::string>& functions, const std::string& filename);

#endif // FUNCTION_DETECTOR_H