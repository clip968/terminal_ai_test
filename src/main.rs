use dialoguer::{Input, Select};
use regex::Regex;
use reqwest::blocking::Client;
use serde::{Deserialize, Serialize};
use std::io::{self, BufRead, BufReader, Write};
use std::process::{Command, Stdio};
use std::thread;
use rustyline::completion::FilenameCompleter;
use rustyline::error::ReadlineError;
use rustyline::{Config, CompletionType, Editor};
use rustyline_derive::{Completer, Helper, Hinter, Validator, Highlighter};

#[derive(Deserialize)]
struct ModelInfo {
    name: String,
}

#[derive(Deserialize)]
struct TagsResponse {
    models: Vec<ModelInfo>,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
struct Message {
    role: String,
    content: String,
}

#[derive(Serialize)]
struct ChatRequest {
    model: String,
    messages: Vec<Message>,
    stream: bool,
}

#[derive(Deserialize, Debug)]
struct ChatResponse {
    message: Option<Message>,
    error: Option<String>,
}

#[derive(Helper, Completer, Hinter, Validator, Highlighter)]
struct MyHelper {
    #[rustyline(Completer)]
    completer: FilenameCompleter,
}

fn main() {
    let client = Client::new();

    // --- 1. Ollama 모델 목록 가져오기 ---
    let tag_res = client.get("http://localhost:11434/api/tags").send();

    if tag_res.is_err() {
        eprintln!("Error: Ollama가 실행 중인지 확인하세요. (ollama serve)");
        wait_for_enter();
        return;
    }

    let tag_res: TagsResponse = tag_res.unwrap().json().expect("JSON 파싱 실패");
    let models: Vec<String> = tag_res.models.into_iter().map(|m| m.name).collect();
    let re_think = Regex::new(r"(?s)<think>(.*?)</think>").unwrap();

    if models.is_empty() {
        eprintln!("설치된 Ollama 모델이 없습니다.");
        wait_for_enter();
        return;
    }

    // --- 2. 모델 선택 UI ---
    let selection = Select::new()
        .with_prompt("Ollama Model 선택")
        .items(&models)
        .interact()
        .unwrap();
    let model = models[selection].clone();

    // --- 3. Kitty Selection (드래그한 텍스트 가져오기) ---
    let selected_output = Command::new("kitty")
        .args(["@", "get-text", "--selection", "primary"])
        .output()
        .unwrap_or_else(|_| std::process::Output {
            status: std::os::unix::process::ExitStatusExt::from_raw(0),
            stdout: vec![],
            stderr: vec![],
        });

    let initial_prompt = String::from_utf8_lossy(&selected_output.stdout)
        .trim()
        .to_string();

    // --- 4. 시스템 프롬프트 (Agent 설정) ---
    // [수정됨] 백틱 3개를 직접 쓰지 않고 변수로 만들어서 주입합니다.
    // 소스 코드 상에 ` ``` `가 직접 등장하지 않아 문법이 깨지지 않습니다.
    let ticks = "`".repeat(3); 
    
    let system_instruction = format!(r#"
    You are a Linux Terminal Assistant running on Arch Linux (Fish Shell).
    
    [IMPORTANT RULES]
    1. Before answering, you MUST provide your thinking process enclosed in <think> and </think> tags.
    2. If the user asks to perform a system action, you MUST output the command inside a code block labeled 'execute'.
    
    Example:
    <think>
    User wants to update npm. I need to use the global flag.
    </think>
    
    {}execute
    npm update -g
    {}
    
    Do NOT ask for permission in text. Just provide the execute block.
    "#, ticks, ticks);

    let mut history: Vec<Message> = Vec::new();

    history.push(Message {
        role: "system".to_string(),
        content: system_instruction,
    });

    println!("\n=== {} (Agent Mode) 시작 (종료: Ctrl+C) ===", model);

    let mut next_input = if !initial_prompt.is_empty() {
        println!("> 선택된 텍스트로 시작: {}", initial_prompt);
        initial_prompt
    } else {
        String::new()
    };

    // [수정됨] 정규표현식도 ticks 변수를 사용하여 생성
    let regex_str = format!(r"{}execute\s*([\s\S]*?)\s*{}", ticks, ticks);
    let re_execute = Regex::new(&regex_str).unwrap();

    // --- Rustyline Setup ---
    let config = Config::builder()
        .completion_type(CompletionType::List)
        .build();
    let h = MyHelper {
        completer: FilenameCompleter::new(),
    };
    let mut rl = Editor::with_config(config).expect("Unable to create editor");
    rl.set_helper(Some(h));

    // --- 5. 대화 루프 (Session Loop) ---
    enum Mode {
        Agent,
        Shell,
    }

    let mut current_mode = Mode::Agent;

    loop {
        let input = if !next_input.is_empty() {
            let s = next_input.clone();
            next_input.clear();
            s
        } else {
            // 프롬프트 표시
            let prompt = match current_mode {
                Mode::Agent => "\n(Agent) >>> ",
                Mode::Shell => "\n(Shell) $ ",
            };

            match rl.readline(prompt) {
                Ok(line) => {
                    let line = line.trim().to_string();
                    if !line.is_empty() {
                        let _ = rl.add_history_entry(line.as_str());
                    }
                    line
                }
                Err(ReadlineError::Interrupted) => {
                    println!("CTRL-C");
                    break;
                }
                Err(ReadlineError::Eof) => {
                    println!("CTRL-D");
                    break;
                }
                Err(err) => {
                    println!("Error: {:?}", err);
                    break;
                }
            }
        };

        if input.eq_ignore_ascii_case("exit") || input.eq_ignore_ascii_case("quit") {
            println!("Bye!");
            break;
        }

        if input.is_empty() {
            continue;
        }

        // 모드 전환 명령어 처리
        if input == "!shell" {
            current_mode = Mode::Shell;
            println!("Switched to Shell Mode. (Type '!agent' to switch back)");
            continue;
        } else if input == "!agent" {
            current_mode = Mode::Agent;
            println!("Switched to Agent Mode.");
            continue;
        }

        match current_mode {
            Mode::Shell => {
                // 쉘 명령어 직접 실행 (Streaming Output)
                let child = Command::new("fish")
                    .arg("-c")
                    .arg(&input)
                    .stdout(Stdio::piped())
                    .stderr(Stdio::piped())
                    .spawn();

                match child {
                    Ok(mut child) => {
                        let stdout = child.stdout.take().expect("Failed to capture stdout");
                        let stderr = child.stderr.take().expect("Failed to capture stderr");

                        // Stderr를 별도 스레드에서 읽어서 데드락 방지 및 동시 출력
                        let stderr_handle = thread::spawn(move || {
                            let mut reader = BufReader::new(stderr);
                            let mut line = String::new();
                            let mut log = String::new();
                            while let Ok(n) = reader.read_line(&mut line) {
                                if n == 0 { break; }
                                print!("{}", line);
                                log.push_str(&line);
                                line.clear();
                            }
                            log
                        });

                        let mut output_log = String::new();
                        let mut reader_out = BufReader::new(stdout);
                        let mut line = String::new();
                        while let Ok(n) = reader_out.read_line(&mut line) {
                            if n == 0 { break; }
                            print!("{}", line);
                            output_log.push_str(&line);
                            line.clear();
                        }

                        let err_log = stderr_handle.join().unwrap_or_default();
                        let _ = child.wait();

                        // 실행 기록을 History에 추가
                        history.push(Message {
                            role: "user".to_string(),
                            content: format!("Executed Shell Command: {}\nOutput:\n{}{}", input, output_log, err_log),
                        });
                    }
                    Err(e) => eprintln!("Command failed to start: {}", e),
                }
            }
            Mode::Agent => {
                // 기존 Agent 로직
                history.push(Message {
                    role: "user".to_string(),
                    content: input.clone(),
                });

                print!("Thinking...");
                io::stdout().flush().unwrap();

                let res = client
                    .post("http://localhost:11434/api/chat")
                    .json(&ChatRequest {
                        model: model.clone(),
                        messages: history.clone(),
                        stream: false,
                    })
                    .send();

                print!("\r\x1b[K");

                match res {
                    Ok(response) => {
                        match response.json::<ChatResponse>() {
                            Ok(json_resp) => {
                                // 1. 에러 체크
                                if let Some(err_msg) = json_resp.error {
                                    eprintln!("\n[Ollama Error] {}", err_msg);
                                    continue;
                                }

                                // 2. 메시지 처리
                                if let Some(ai_msg) = json_resp.message {
                                    let ai_content = ai_msg.content.clone();
                                    
                                    if let Some(caps) = re_think.captures(&ai_content) {
                                          let thought_process = &caps[1];

                                          let final_answer = re_think.replace(&ai_content, "").to_string();
                                        
                                        println!("\n\x1b[1;90m🧠 Thinking Process:\x1b[0m");
                                        println!("\x1b[90m\x1b[3m{}\x1b[0m\n", thought_process.trim());
                                        println!("\x1b[1;90m----------------------------------------\x1b[0m\n");

                                        println!("{}", final_answer.trim());
                                    } else {
                                        println!("{}", ai_content);
                                    }

                                    history.push(ai_msg);

                                    // --- 6. 명령어 실행 로직 ---
                                    if let Some(caps) = re_execute.captures(&ai_content) {
                                        let command_to_run = caps[1].trim(); 

                                        println!("\n[!] AI가 다음 명령어를 실행하려고 합니다:");
                                        println!("\x1b[33m{}\x1b[0m", command_to_run);

                                        let confirm = Input::<String>::new()
                                            .with_prompt("실행하시겠습니까? (y/n)")
                                            .default("n".into())
                                            .interact()
                                            .unwrap();

                                        if confirm.to_lowercase() == "y" {
                                            println!("Running...");
                                            
                                            let output = Command::new("fish")
                                                .arg("-c")
                                                .arg(command_to_run)
                                                .output();

                                            match output {
                                                Ok(out) => {
                                                    let stdout = String::from_utf8_lossy(&out.stdout);
                                                    let stderr = String::from_utf8_lossy(&out.stderr);

                                                    println!("-- Output --\n{}{}", stdout, stderr);

                                                    let result_msg = format!(
                                                        "Command Executed.\nSTDOUT:\n{}\nSTDERR:\n{}",
                                                        stdout, stderr
                                                    );

                                                    history.push(Message {
                                                        role: "user".to_string(),
                                                        content: format!("System Output: {}", result_msg),
                                                    });

                                                    // println!("\n(결과를 AI에게 전송 중...)");
                                                    // next_input = "결과를 확인하고 다음 단계를 알려줘.".to_string();
                                                    // 자동 응답 대신 루프 처음으로 돌아가서 사용자 입력 대기
                                                }
                                                Err(e) => {
                                                    eprintln!("실행 실패: {}", e);
                                                    // next_input = format!("명령어 실행 실패: {}", e);
                                                }
                                            }
                                        } else {
                                            println!("실행 취소됨.");
                                            history.push(Message {
                                                role: "user".to_string(),
                                                content: "User cancelled the command execution.".to_string(),
                                            });
                                        }
                                    }
                                } else {
                                    eprintln!("\n[Error] 응답에 message도 error도 없습니다.");
                                }
                            }
                            Err(e) => eprintln!("\n[Error] JSON 파싱 실패: {}", e),
                        }
                    }
                    Err(e) => eprintln!("\n[Error] HTTP 요청 실패: {}", e),
                }
            }
        }
    }
}

fn wait_for_enter() {
    println!("\n엔터키를 누르면 종료합니다...");
    let mut s = String::new();
    std::io::stdin().read_line(&mut s).unwrap();
}
