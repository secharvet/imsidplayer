
bool Config::save() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        path = m_configFilePath;
    }
    
    if (path.empty()) {
        return false;
    }
    
    return save(path);
}
