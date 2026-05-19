cd "TD-Learning+N-tuple Network"
g++ -O3 -shared -fPIC -march=native -fopenmp base_agent/base_agent.cpp -o base_agent/base_agent.dll;
g++ -O3 -shared -fPIC -march=native -fopenmp 8k4k_agent/8k4k_agent.cpp -o 8k4k_agent/8k4k_agent.dll;
g++ -O3 -shared -fPIC -march=native -fopenmp 16k8k4k_agent/16k8k4k_agent.cpp -o 16k8k4k_agent/16k8k4k_agent.dll;
