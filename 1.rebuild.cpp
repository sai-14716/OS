#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sstream>

using namespace std;

vector<int> fetch_dependencies(int module, const string &file_name) 
{
    ifstream file(file_name);
    if (!file) 
    {
        cerr << "Error opening " << file_name << endl;
        exit(1);
    }

    string line;
    for (int i = 0; i <= module; ++i) 
    {
        if (!getline(file, line)) 
        {
            cerr << "Problem reading dependencies for module " << module + 1 << endl;
            exit(1);
        }
    }

    istringstream ss(line);
    int mod, dependency;
    vector<int> deps;
    ss >> mod;
    while (ss >> dependency) 
    {
        deps.push_back(dependency - 1); // 0-indexed
    }
    return deps;
}

void read_done(int total_modules, vector<int> &done_list) 
{
    ifstream file("done.txt");
    if (file) 
    {
        for (int i = 0; i < total_modules; ++i) 
            file >> done_list[i];
    } 
    
    else 
    {
        fill(done_list.begin(), done_list.end(), 0);
    }
}

void write_done(int total_modules, const vector<int> &done_list) 
{
    ofstream file("done.txt");
    for (int i = 0; i < total_modules; ++i)
        file << done_list[i] << " ";
}

void rebuild(int total_modules, const string &file_name, int module, bool root_call) 
{
    vector<int> status(total_modules);
    read_done(total_modules, status);

    if (root_call) 
    {
        fill(status.begin(), status.end(), 0);
        write_done(total_modules, status);
    }

    string rebuilt_from = "";
    vector<int> deps = fetch_dependencies(module, file_name);

    for (int dep : deps) 
    {
        if (status[dep] == 0) 
        {
            pid_t pid = fork();

            if (pid == 0) 
            {
                char arg[10];
                snprintf(arg, sizeof(arg), "%d", dep + 1);
                char *args[] = {(char *)"rebuild", arg, NULL};
                execv("./rebuild", args);
                exit(0);
            } 
            
            else 
            {
                wait(NULL);
                if (!rebuilt_from.empty())
                    rebuilt_from += ", ";
                rebuilt_from += "foo" + to_string(dep + 1);
            }
        }
    }

    status[module] = 1;
    write_done(total_modules, status);

    if (!rebuilt_from.empty()) 
    {
        cout << "foo" << module + 1 << " rebuilt from " << rebuilt_from << endl;
    } 
    
    else 
    {
        cout << "foo" << module + 1 << " rebuilt" << endl;
    }
}

int main(int argc, char *argv[]) 
{
    
    if (argc < 2) 
    {
        cerr << "Usage: " << argv[0] << " <module_number>" << endl;
        return 1;
    }

    int module_id = stoi(argv[1]) - 1;

    ifstream dependency_file("foodep.txt");
    if (!dependency_file) 
    {
        cerr << "Error opening foodep.txt" << endl;
        return 1;
    }

    int module_count;
    dependency_file >> module_count;
    dependency_file.close();

    bool root_call = (argc == 2);
    rebuild(module_count, "foodep.txt", module_id, root_call);

    return 0;
}
