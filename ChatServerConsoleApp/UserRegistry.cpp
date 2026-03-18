#include "UserRegistry.h"

void UserRegistry::setCapacity(int cap)
{
    capacity = cap;
}

bool UserRegistry::isFull() const
{
    return (int)users.size() >= capacity;
}

bool UserRegistry::registerUser(const std::string& username, const std::string& password)
{
    // If the username already exists, registration fails
    if (users.count(username) > 0)
        return false;

    users[username] = password;
    return true;
}

bool UserRegistry::userExists(const std::string& username) const
{
    return users.count(username) > 0;
}

bool UserRegistry::authenticate(const std::string& username, const std::string& password) const
{
    auto it = users.find(username);
    if (it == users.end())
        return false;
    return it->second == password;
}

int UserRegistry::getUserCount() const
{
    return (int)users.size();
}