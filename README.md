# OneNode
Your perfect ecosystem maker across different OS

`What does CoreBridge do?`

# **CoreBridge has the following methods:**
    void handleNewConnection();
    void readClientData();
    void processFileData();
    
# **handleNewConnection() :**
    This method is responsible for connecting new clients to the server (or the other device). It does it through adding a clinet to the list of clients after validating and authenticating. This helps in hassle-free connection without any authentication on the next pairing.
    
# **readClientData() :**
    This method involves a lot of actions in turn. They are
        - Reads all the data arrived on the buffer
        - Processes the available data on the buffer and writes them on th disk when a file transfer process is active
        - Calculates header size of the TCP stream if there is no active file transfer taking place
        - Implementing the destination folder
        - File writing onto the disk
        - Checking again for any remaining data on the buffer
        
    This entire method contains validations for all the actions.
