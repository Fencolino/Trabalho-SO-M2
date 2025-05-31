#include <iostream>
#include <fstream>
#include <queue>
#include <vector>
#include <string>
#include <sstream>
#include <limits>    // Para limpar buffer do cin
#include <iomanip>   // Para formatar saída
#include <cmath>     // Para arredondar
#include <algorithm> // Para ordenar
#include <numeric>   // Para std::accumulate (agora substituído por loop)

using namespace std;

//---------------------------------------------------------------------------
// Estruturas de Dados e Enumerações (Sem alterações)
//---------------------------------------------------------------------------

enum class EstagioTarefa { ESPERA_INICIAL, PRONTA, EXECUTANDO_ETAPA1, BLOQUEADA_IO, EXECUTANDO_ETAPA2, FINALIZADA };

struct InfoTarefa {
    int id_unico;
    int instante_chegada;
    int tempo_cpu_etapa1;
    bool requer_io;
    int duracao_espera_io;
    int tempo_cpu_etapa2;
    int prioridade_tarefa = 1;
    int tempo_restante_etapa_atual;
    int quantum_designado;
    int quantum_restante_ciclo;
    EstagioTarefa estagio_corrente;
    int tempo_restante_bloqueio;
    int instante_primeira_execucao = -1;
    int instante_conclusao = -1;
    int num_trocas_contexto = 0;
    int tempo_total_fila_prontos = 0;
    int tempo_total_cpu_necessario;
    int tempo_permanencia_sistema = 0;
    int ultimo_instante_fila_prontos = -1;
};

struct SegmentoGantt {
    int inicio;
    int fim;
    int id_tarefa_executada;
    int id_nucleo_usado;
};

struct ConfiguracaoSimulacao {
    int quantum_base = 4;
    int num_cpus = 2;
    bool usar_quantum_variavel = false;
};

struct EstadoSimulacao {
    int tempo_atual = 0;
    vector<InfoTarefa> tarefas_configuradas;
    queue<InfoTarefa*> fila_prontas;
    queue<InfoTarefa*> fila_bloqueadas_io;
    vector<InfoTarefa*> tarefa_em_execucao;
    size_t proximo_indice_chegada = 0;
};

struct ResultadosSimulacao {
    vector<InfoTarefa> tarefas_finalizadas;
    vector<SegmentoGantt> log_gantt;
    int total_trocas_contexto_geral = 0;
    double tempo_cpu_ocupado_total = 0;
};

//---------------------------------------------------------------------------
// Variáveis Globais (Sem alterações)
//---------------------------------------------------------------------------

ConfiguracaoSimulacao g_config;
EstadoSimulacao g_estado;

//---------------------------------------------------------------------------
// Funções Utilitárias e Auxiliares (Sem alterações de estilo aqui)
//---------------------------------------------------------------------------

inline int obterMaior(int val1, int val2) {
    return (val1 > val2) ? val1 : val2;
}

void limparEntradaConsole() {
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

int calcularQuantumTarefa(const InfoTarefa* tarefa) {
    if (!g_config.usar_quantum_variavel) {
        return g_config.quantum_base;
    }
    return obterMaior(1, g_config.quantum_base * tarefa->prioridade_tarefa);
}

void registrarLogGantt(ResultadosSimulacao& resultados, int id_cpu, int t_inicio, int t_fim, int id_tarefa) {
    if (t_inicio < t_fim) {
        resultados.log_gantt.push_back({t_inicio, t_fim, id_tarefa, id_cpu});
    }
}

//---------------------------------------------------------------------------
// Leitura e Configuração Inicial (Função obterConfiguracaoUsuario modificada)
//---------------------------------------------------------------------------

bool lerConfiguracaoTarefas(const string& caminhoArquivo) {
    ifstream arquivo(caminhoArquivo);
    if (!arquivo.is_open()) {
        cerr << "Erro fatal: Nao foi possivel abrir o arquivo de tarefas '" << caminhoArquivo << "'." << endl;
        return false;
    }

    g_estado.tarefas_configuradas.clear();
    string linha, cabecalho;
    getline(arquivo, cabecalho);

    int numLinha = 1;
    while (getline(arquivo, linha)) {
        numLinha++;
        if (linha.empty() || linha[0] == '#') continue;

        stringstream ss(linha);
        InfoTarefa t;
        int io_flag;
        char delimitador;
        int prioridade_lida = 1;

        if (!(ss >> t.id_unico >> delimitador >> t.instante_chegada >> delimitador >> t.tempo_cpu_etapa1 >> delimitador >> io_flag >> delimitador >> t.duracao_espera_io >> delimitador >> t.tempo_cpu_etapa2)) {
            cerr << "Erro de formato na linha " << numLinha << ": " << linha << endl;
            continue;
        }

        if (g_config.usar_quantum_variavel) {
            // Correção: Adiciona checagem explícita do delimitador e EOF, como no original
            if (ss.peek() == EOF || !(ss >> delimitador >> prioridade_lida) || delimitador != '|') {
                cerr << "Erro: Modo quantum variavel ativo, mas coluna Prioridade nao encontrada ou invalida na linha " << numLinha << ": " << linha << endl;
                cerr << "       Certifique-se que o arquivo contem a coluna extra \"| Prioridade\" neste modo." << endl;
                g_estado.tarefas_configuradas.clear(); // Limpa o vetor para indicar falha
                return false;
            }
            t.prioridade_tarefa = (prioridade_lida > 0) ? prioridade_lida : 1;
            if (prioridade_lida <= 0) {
                 cerr << "Aviso: Prioridade invalida (<=0) na linha " << numLinha << ". Usando prioridade 1." << endl;
            }
        } else {
             // Garante que a prioridade seja 1 se não estiver usando quantum variável
             t.prioridade_tarefa = 1;
        }

        t.requer_io = (io_flag != 0);
        t.estagio_corrente = EstagioTarefa::ESPERA_INICIAL;
        t.tempo_restante_etapa_atual = t.tempo_cpu_etapa1;
        t.quantum_designado = 0;
        t.quantum_restante_ciclo = 0;
        t.tempo_restante_bloqueio = 0;
        t.tempo_total_cpu_necessario = t.tempo_cpu_etapa1 + (t.requer_io ? t.tempo_cpu_etapa2 : 0);
        t.ultimo_instante_fila_prontos = t.instante_chegada;
        g_estado.tarefas_configuradas.push_back(t);
    }
    arquivo.close();
    return !g_estado.tarefas_configuradas.empty();
}

// MODIFICADA: Usa do-while para validação de entrada
void obterConfiguracaoUsuario() {
    string nomeArquivo;
    char escolhaQuantum;
    int numNucleos;
    bool entradaValida;

    cout << "Informe o nome do arquivo com a lista de tarefas: ";
    cin >> nomeArquivo;
    limparEntradaConsole();

    do {
        cout << "Usar quantum variavel (s/n)? ";
        cin >> escolhaQuantum;
        limparEntradaConsole();
        escolhaQuantum = tolower(escolhaQuantum);
        entradaValida = (escolhaQuantum == 's' || escolhaQuantum == 'n');
        if (!entradaValida) {
            cout << "    Opcao invalida. Digite 's' ou 'n'." << endl;
        }
    } while (!entradaValida);
    g_config.usar_quantum_variavel = (escolhaQuantum == 's');

    do {
        cout << "Informe o valor base para o quantum (>= 1): ";
        entradaValida = (cin >> g_config.quantum_base) && (g_config.quantum_base >= 1);
        if (!entradaValida) {
            cout << "    Entrada invalida. Digite um numero >= 1." << endl;
            cin.clear(); // Limpa flags de erro do cin
        }
        limparEntradaConsole(); // Limpa o buffer independentemente do sucesso
    } while (!entradaValida);

    do {
        cout << "Informe a quantidade de nucleos da CPU (>= 1): ";
        entradaValida = (cin >> numNucleos) && (numNucleos >= 1);
        if (!entradaValida) {
            cout << "    Entrada invalida. Digite um numero >= 1." << endl;
            cin.clear();
        }
        limparEntradaConsole();
    } while (!entradaValida);
    g_config.num_cpus = numNucleos;

    if (!lerConfiguracaoTarefas(nomeArquivo)) {
        cerr << "[!] Falha critica ao carregar tarefas. Encerrando." << endl;
        exit(1);
    }
}

//---------------------------------------------------------------------------
// Lógica Principal da Simulação (Função atualizarTempoEsperaFila modificada)
//---------------------------------------------------------------------------

void processarChegadas() {
    while(g_estado.proximo_indice_chegada < g_estado.tarefas_configuradas.size() &&
          g_estado.tarefas_configuradas[g_estado.proximo_indice_chegada].instante_chegada <= g_estado.tempo_atual)
    {
        InfoTarefa* nova_tarefa_pronta = &g_estado.tarefas_configuradas[g_estado.proximo_indice_chegada];
        nova_tarefa_pronta->estagio_corrente = EstagioTarefa::PRONTA;
        nova_tarefa_pronta->quantum_designado = calcularQuantumTarefa(nova_tarefa_pronta);
        nova_tarefa_pronta->quantum_restante_ciclo = nova_tarefa_pronta->quantum_designado;
        nova_tarefa_pronta->ultimo_instante_fila_prontos = g_estado.tempo_atual;
        g_estado.fila_prontas.push(nova_tarefa_pronta);
        g_estado.proximo_indice_chegada++;
    }
}

void processarBloqueiosIO() {
    int n = g_estado.fila_bloqueadas_io.size();
    for (int i = 0; i < n; ++i) {
        InfoTarefa* t = g_estado.fila_bloqueadas_io.front();
        g_estado.fila_bloqueadas_io.pop();
        t->tempo_restante_bloqueio--;
        if (t->tempo_restante_bloqueio <= 0) {
            t->estagio_corrente = EstagioTarefa::PRONTA;
            t->tempo_restante_etapa_atual = t->tempo_cpu_etapa2;
            t->quantum_designado = calcularQuantumTarefa(t);
            t->quantum_restante_ciclo = t->quantum_designado;
            t->ultimo_instante_fila_prontos = g_estado.tempo_atual;
            g_estado.fila_prontas.push(t);
        } else {
            g_estado.fila_bloqueadas_io.push(t);
        }
    }
}

// MODIFICADA: Usa while para iterar sobre a fila
void atualizarTempoEsperaFila() {
    int n = g_estado.fila_prontas.size();
    int contador = 0;
    while (contador < n) {
        InfoTarefa* t = g_estado.fila_prontas.front();
        g_estado.fila_prontas.pop();
        t->tempo_total_fila_prontos++;
        g_estado.fila_prontas.push(t);
        contador++;
    }
}

void executarTarefaEmNucleo(ResultadosSimulacao& resultados, int id_nucleo, int& tarefas_restantes, vector<int>& inicio_burst_cpu) {
    InfoTarefa* tarefa_atual = g_estado.tarefa_em_execucao[id_nucleo];

    if (tarefa_atual == nullptr && !g_estado.fila_prontas.empty()) {
        tarefa_atual = g_estado.fila_prontas.front();
        g_estado.fila_prontas.pop();
        g_estado.tarefa_em_execucao[id_nucleo] = tarefa_atual;
        resultados.total_trocas_contexto_geral++;
        tarefa_atual->num_trocas_contexto++;

        if (tarefa_atual->instante_primeira_execucao == -1) {
            tarefa_atual->instante_primeira_execucao = g_estado.tempo_atual;
        }

        tarefa_atual->estagio_corrente = (tarefa_atual->tempo_restante_etapa_atual == tarefa_atual->tempo_cpu_etapa1)
                                         ? EstagioTarefa::EXECUTANDO_ETAPA1
                                         : EstagioTarefa::EXECUTANDO_ETAPA2;
        inicio_burst_cpu[id_nucleo] = g_estado.tempo_atual;
    }

    if (tarefa_atual != nullptr) {
        resultados.tempo_cpu_ocupado_total++;
        tarefa_atual->tempo_restante_etapa_atual--;
        tarefa_atual->quantum_restante_ciclo--;

        bool etapa_concluida = (tarefa_atual->tempo_restante_etapa_atual <= 0);
        bool quantum_expirado = (tarefa_atual->quantum_restante_ciclo <= 0);

        if (etapa_concluida || quantum_expirado) {
            registrarLogGantt(resultados, id_nucleo, inicio_burst_cpu[id_nucleo], g_estado.tempo_atual + 1, tarefa_atual->id_unico);
            inicio_burst_cpu[id_nucleo] = -1;

            if (etapa_concluida) {
                if (tarefa_atual->estagio_corrente == EstagioTarefa::EXECUTANDO_ETAPA1 && tarefa_atual->requer_io) {
                    tarefa_atual->estagio_corrente = EstagioTarefa::BLOQUEADA_IO;
                    tarefa_atual->tempo_restante_bloqueio = tarefa_atual->duracao_espera_io;
                    g_estado.fila_bloqueadas_io.push(tarefa_atual);
                } else {
                    tarefa_atual->estagio_corrente = EstagioTarefa::FINALIZADA;
                    tarefa_atual->instante_conclusao = g_estado.tempo_atual + 1;
                    tarefa_atual->tempo_permanencia_sistema = tarefa_atual->instante_conclusao - tarefa_atual->instante_chegada;
                    resultados.tarefas_finalizadas.push_back(*tarefa_atual);
                    tarefas_restantes--;
                }
            } else { // Quantum expirou
                tarefa_atual->estagio_corrente = EstagioTarefa::PRONTA;
                tarefa_atual->quantum_designado = calcularQuantumTarefa(tarefa_atual);
                tarefa_atual->quantum_restante_ciclo = tarefa_atual->quantum_designado;
                tarefa_atual->ultimo_instante_fila_prontos = g_estado.tempo_atual + 1;
                g_estado.fila_prontas.push(tarefa_atual);
            }
            g_estado.tarefa_em_execucao[id_nucleo] = nullptr;
        }
    }
}

void rodarSimulacao(ResultadosSimulacao& resultados) {
    int tarefas_pendentes = g_estado.tarefas_configuradas.size();
    vector<int> inicio_burst_cpu(g_config.num_cpus, -1);
    g_estado.tarefa_em_execucao.assign(g_config.num_cpus, nullptr);

    const int LIMITE_TEMPO_SEGURANCA = 30000;
    while (tarefas_pendentes > 0 && g_estado.tempo_atual <= LIMITE_TEMPO_SEGURANCA) {
        processarChegadas();
        processarBloqueiosIO();
        atualizarTempoEsperaFila();

        for (int i = 0; i < g_config.num_cpus; ++i) {
            executarTarefaEmNucleo(resultados, i, tarefas_pendentes, inicio_burst_cpu);
        }

        g_estado.tempo_atual++;

        if (tarefas_pendentes <= 0) {
            bool nucleo_ocupado = false;
            for(int k=0; k < g_config.num_cpus; ++k) {
                if (g_estado.tarefa_em_execucao[k] != nullptr) {
                    nucleo_ocupado = true;
                    if(inicio_burst_cpu[k] != -1) {
                         registrarLogGantt(resultados, k, inicio_burst_cpu[k], g_estado.tempo_atual, g_estado.tarefa_em_execucao[k]->id_unico);
                    }
                }
            }
            if (!nucleo_ocupado && g_estado.fila_prontas.empty() && g_estado.fila_bloqueadas_io.empty()) {
                 bool alguma_tarefa_nao_chegou = false;
                 for(const auto& t : g_estado.tarefas_configuradas) {
                     if(t.estagio_corrente == EstagioTarefa::ESPERA_INICIAL) {
                         alguma_tarefa_nao_chegou = true;
                         break;
                     }
                 }
                 if (!alguma_tarefa_nao_chegou) break;
            }
        }
    }

    if (g_estado.tempo_atual > LIMITE_TEMPO_SEGURANCA) {
        cerr << "Erro: Simulacao excedeu o limite de tempo (" << LIMITE_TEMPO_SEGURANCA << ")!" << endl;
        for(int k=0; k < g_config.num_cpus; ++k) {
            if (g_estado.tarefa_em_execucao[k] != nullptr && inicio_burst_cpu[k] != -1) {
                registrarLogGantt(resultados, k, inicio_burst_cpu[k], g_estado.tempo_atual, g_estado.tarefa_em_execucao[k]->id_unico);
            }
        }
    }
}

//---------------------------------------------------------------------------
// Funções de Saída e Relatórios (Função exibirTabelaResumo modificada)
//---------------------------------------------------------------------------

// MODIFICADA: Usa loops for explícitos em vez de std::accumulate
void exibirTabelaResumo(const ResultadosSimulacao& resultados) {
    if (resultados.tarefas_finalizadas.empty()) {
        cout << "Nenhuma tarefa concluida." << endl;
        return;
    }

    // Cálculo das somas com loops explícitos
    double soma_ciclo = 0.0;
    double soma_espera = 0.0;
    for (const auto& t : resultados.tarefas_finalizadas) {
        soma_ciclo += t.tempo_permanencia_sistema;
        soma_espera += t.tempo_total_fila_prontos;
    }

    double ciclo_medio = soma_ciclo / resultados.tarefas_finalizadas.size();
    double espera_media = soma_espera / resultados.tarefas_finalizadas.size();
    double uso_cpu_percentual = (g_estado.tempo_atual > 0 && g_config.num_cpus > 0) ? (resultados.tempo_cpu_ocupado_total / (double)(g_config.num_cpus * g_estado.tempo_atual)) * 100.0 : 0.0;

    cout << "\n--- Metricas Globais ---" << endl;
    cout << fixed << setprecision(0);
    cout << "Tempo de Espera Total: " << static_cast<long long>(soma_espera) << endl;
    cout << "Turnaround Total: " << static_cast<long long>(soma_ciclo) << endl;
    cout << fixed << setprecision(2);
    cout << "Tempo de Espera Medio: " << espera_media << endl;
    cout << "Turnaround Medio: " << ciclo_medio << endl;
    cout << "Uso Medio da CPU: " << uso_cpu_percentual << "%" << endl;
    cout << fixed << setprecision(0);
    cout << "Numero Total de Trocas de Contexto: " << resultados.total_trocas_contexto_geral << endl;
    cout << "Tempo Total de Simulacao: " << g_estado.tempo_atual << endl;
}

void desenharGraficoGantt(const ResultadosSimulacao& resultados) {
    cout << "\n--- Linha do Tempo (Gantt) ---" << endl;

    if (resultados.log_gantt.empty() && g_estado.tempo_atual == 0) {
         cout << "Nenhuma atividade registrada." << endl;
         return;
    }
    if (g_estado.tempo_atual == 0) return;

    vector<SegmentoGantt> log_ordenado = resultados.log_gantt;
    sort(log_ordenado.begin(), log_ordenado.end(), [](const SegmentoGantt& a, const SegmentoGantt& b) {
        if (a.inicio != b.inicio) return a.inicio < b.inicio;
        return a.id_nucleo_usado < b.id_nucleo_usado;
    });

    int tempo_max = g_estado.tempo_atual;
    int largura_id_max = 2;
    for(const auto& reg : log_ordenado) {
        largura_id_max = max(largura_id_max, (int)to_string(reg.id_tarefa_executada).length() + 1);
    }
    largura_id_max = min(largura_id_max, 4);

    vector<string> linhas_gantt(g_config.num_cpus);
    int largura_rotulo_nucleo = 9;
    int largura_total_grafico = tempo_max * (largura_id_max + 1);

    for (int i = 0; i < g_config.num_cpus; ++i) {
        stringstream ss;
        ss << "Nucleo " << i << ": ";
        linhas_gantt[i] = ss.str();
        linhas_gantt[i].resize(largura_rotulo_nucleo + largura_total_grafico, ' ');
    }

    for (const auto& reg : log_ordenado) {
        if (reg.id_nucleo_usado >= 0 && reg.id_nucleo_usado < g_config.num_cpus) {
            string id_str = "P" + to_string(reg.id_tarefa_executada);
            id_str.resize(largura_id_max, ' ');
            if (id_str.length() > largura_id_max) id_str[largura_id_max-1] = '*';

            for (int t = reg.inicio; t < reg.fim; ++t) {
                int pos_base = largura_rotulo_nucleo + t * (largura_id_max + 1);
                for(int k=0; k < largura_id_max; ++k) {
                    if (pos_base + k < linhas_gantt[reg.id_nucleo_usado].length()) {
                        linhas_gantt[reg.id_nucleo_usado][pos_base + k] = id_str[k];
                    }
                }
            }
        }
    }

    for (int i = 0; i < g_config.num_cpus; ++i) {
        for (int t = 0; t < tempo_max; ++t) {
            int pos_base = largura_rotulo_nucleo + t * (largura_id_max + 1);
            bool ocupado = false;
            for(int k=0; k < largura_id_max; ++k) {
                if (pos_base + k >= linhas_gantt[i].length() || linhas_gantt[i][pos_base + k] != ' ') {
                    ocupado = (pos_base + k < linhas_gantt[i].length());
                    break;
                }
            }
            if (!ocupado) {
                for(int k=0; k < largura_id_max; ++k) {
                    if(pos_base + k < linhas_gantt[i].length()) {
                         linhas_gantt[i][pos_base + k] = (k == largura_id_max / 2) ? '-' : ' ';
                    }
                }
            }
        }
    }

    for (int i = 0; i < g_config.num_cpus; ++i) {
        cout << linhas_gantt[i] << endl;
    }

    cout << string(largura_rotulo_nucleo, ' ');
    for(int t = 0; t < tempo_max; ++t) {
        string tempo_str = to_string(t);
        int largura_bloco = largura_id_max + 1;
        if (t % 5 == 0 || t == 0 || t == tempo_max -1) {
             int pad_esq = (largura_bloco - tempo_str.length()) / 2;
             cout << string(pad_esq, ' ') << tempo_str << string(largura_bloco - tempo_str.length() - pad_esq, ' ');
        } else {
             int pad_esq = (largura_bloco - 1) / 2;
             cout << string(pad_esq, ' ') << "." << string(largura_bloco - 1 - pad_esq, ' ');
        }
    }
    cout << endl;
}

void apresentarResultadosCompletos(ResultadosSimulacao& resultados) {
    if (resultados.tarefas_finalizadas.empty()) {
        return;
    }
    sort(resultados.tarefas_finalizadas.begin(), resultados.tarefas_finalizadas.end(), [](const InfoTarefa& a, const InfoTarefa& b) {
        return a.id_unico < b.id_unico;
    });

    exibirTabelaResumo(resultados);
    desenharGraficoGantt(resultados);
}

//---------------------------------------------------------------------------
// Função Principal (main) (Sem alterações)
//---------------------------------------------------------------------------

int main() {
    obterConfiguracaoUsuario();
    ResultadosSimulacao resultados;
    rodarSimulacao(resultados);
    apresentarResultadosCompletos(resultados);
    return 0;
}

