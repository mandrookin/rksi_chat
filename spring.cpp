#include <list>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <cstdlib>

#include "IEncoder.h"

using namespace std;

IEncoder * GetEncoder(int encooder_type);


int main()
{
    string  my_string[] = { "Base", "Gronsfeld", "Debug" };
    list<string>  my_list;
    list<string>::iterator  list_item;
    vector<string> my_array;
    

    for (int i = 0; i < 3; i++) {
        my_list.push_back(my_string[i]);
    }

    my_list.push_front("I am a first string");

    cout << "--- List size is " << my_list.size() << " elements\n" ;
    list_item = my_list.begin();
    for (int i = 0; i < my_list.size(); i++) {
        cout << *list_item << endl;
        my_array.push_back(*list_item);
        list_item++;
    }

    my_array.push_back("Not last string");
    my_array.push_back("Last last string");

    cout << "\n---  Vector size is " << my_array.size() << " elements\n";
    for (auto item : my_array)
        cout << item << endl;

    cout << endl << endl;

    map<string, IEncoder *>  gold;

    int counter = 0;
    for (auto str : my_string)
    {
        IEncoder * enc = GetEncoder(counter++);
        gold.insert(pair<string, IEncoder*>(str, enc));
    }

    gold["Gronsfeld"]->WriteEncooderName();

    return 0;
}