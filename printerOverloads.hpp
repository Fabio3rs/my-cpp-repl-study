#pragma once

void writeHeaderPrintOverloads();

void printdata(std::string_view str, std::string_view name,
               std::string_view type);
void printdata(const std::mutex &mtx, std::string_view name,
               std::string_view type);
void printdata(int val, std::string_view name, std::string_view type);
void printdata(double val, std::string_view name, std::string_view type);
void printdata(float val, std::string_view name, std::string_view type);
void printdata(unsigned int val, std::string_view name, std::string_view type);
