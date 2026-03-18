#pragma once
#include <string>
#include <unordered_map>

// UserRegistry stores all registered usernames and passwords.
class UserRegistry
{
private:
    std::unordered_map<std::string, std::string> users; // username -> password
    int capacity;

public:
    void setCapacity(int cap);
    bool isFull() const;
    bool registerUser(const std::string& username, const std::string& password);
    bool userExists(const std::string& username) const;
    bool authenticate(const std::string& username, const std::string& password) const;
    int getUserCount() const;
};
