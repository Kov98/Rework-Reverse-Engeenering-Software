use serde::Serialize;
use std::env;
use std::ffi::OsString;
use std::fs::File;
use std::io::{self, Read, Write};
use std::path::PathBuf;
use std::process::ExitCode;

const VALUE_LIMIT: usize = 4096;
const BUFFER_SIZE: usize = 64 * 1024;

struct Options {
    path: PathBuf,
    min_length: u64,
    max_strings: usize,
}

#[derive(Serialize)]
struct ExtractedString {
    offset: u64,
    value: String,
}

#[derive(Serialize)]
struct Scan {
    size: u64,
    entropy: f64,
    strings: Vec<ExtractedString>,
    strings_truncated: bool,
}

fn parse_number(value: Option<OsString>, flag: &str) -> Result<u64, String> {
    let text = value.ok_or_else(|| format!("Missing value for {flag}"))?;
    let text = text.to_str().ok_or_else(|| format!("Invalid value for {flag}"))?;
    if text.is_empty() || !text.bytes().all(|byte| byte.is_ascii_digit()) {
        return Err(format!("{flag} requires an unsigned decimal integer"));
    }
    text.parse::<u64>().map_err(|_| format!("Value for {flag} is too large"))
}

fn options() -> Result<Options, String> {
    let mut args = env::args_os().skip(1);
    let path = args.next().ok_or("Usage: bytecore <file> [--min-length N] [--max-strings N]")?;
    let mut options = Options { path: path.into(), min_length: 4, max_strings: 10_000 };
    let mut has_min = false;
    let mut has_max = false;
    while let Some(flag) = args.next() {
        match flag.to_str() {
            Some("--min-length") if !has_min => {
                has_min = true;
                options.min_length = parse_number(args.next(), "--min-length")?;
                if options.min_length == 0 {
                    return Err("--min-length must be positive".into());
                }
            }
            Some("--max-strings") if !has_max => {
                has_max = true;
                options.max_strings = usize::try_from(parse_number(args.next(), "--max-strings")?)
                    .map_err(|_| "--max-strings exceeds this platform's capacity")?;
            }
            _ => return Err(format!("Unknown or repeated flag: {}", flag.to_string_lossy())),
        }
    }
    Ok(options)
}

fn finish_run(scan: &mut Scan, options: &Options, offset: u64, length: u64, prefix: &mut Vec<u8>) {
    if length >= options.min_length {
        if scan.strings.len() < options.max_strings {
            // Every retained byte is printable ASCII, which is valid UTF-8.
            let value = String::from_utf8(std::mem::take(prefix)).expect("ASCII run");
            scan.strings.push(ExtractedString { offset, value });
            if length > VALUE_LIMIT as u64 {
                scan.strings_truncated = true;
            }
        } else {
            scan.strings_truncated = true;
        }
    }
    prefix.clear();
}

fn scan(options: &Options) -> Result<Scan, String> {
    if !std::fs::metadata(&options.path)
        .map_err(|error| format!("Cannot inspect input: {error}"))?
        .is_file()
    {
        return Err("Input must be a regular file".into());
    }
    let mut input = File::open(&options.path).map_err(|error| format!("Cannot open input: {error}"))?;
    if !input.metadata().map_err(|error| format!("Cannot inspect input: {error}"))?.is_file() {
        return Err("Input must be a regular file".into());
    }
    let mut result = Scan { size: 0, entropy: 0.0, strings: Vec::new(), strings_truncated: false };
    let mut histogram = [0_u64; 256];
    let mut buffer = [0_u8; BUFFER_SIZE];
    let mut run_start = 0;
    let mut run_length = 0;
    let mut prefix = Vec::new();
    loop {
        let count = match input.read(&mut buffer) {
            Ok(0) => break,
            Ok(count) => count,
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            Err(error) => return Err(format!("Cannot read input: {error}")),
        };
        for &byte in &buffer[..count] {
            histogram[byte as usize] += 1;
            if (0x20..=0x7e).contains(&byte) {
                if run_length == 0 {
                    run_start = result.size;
                }
                run_length += 1;
                if result.strings.len() < options.max_strings && prefix.len() < VALUE_LIMIT {
                    prefix.push(byte);
                }
            } else {
                finish_run(&mut result, options, run_start, run_length, &mut prefix);
                run_length = 0;
            }
            result.size = result.size.checked_add(1).ok_or("Input exceeds 64-bit offset range")?;
        }
    }
    finish_run(&mut result, options, run_start, run_length, &mut prefix);
    if result.size > 0 {
        for count in histogram.into_iter().filter(|&count| count > 0) {
            let probability = count as f64 / result.size as f64;
            result.entropy -= probability * probability.log2();
        }
        result.entropy = result.entropy.clamp(0.0, 8.0);
    }
    Ok(result)
}

fn run() -> Result<(), String> {
    let result = scan(&options()?)?;
    let mut output = io::BufWriter::new(io::stdout().lock());
    serde_json::to_writer(&mut output, &result).map_err(|error| format!("Cannot write result: {error}"))?;
    output.write_all(b"\n").and_then(|_| output.flush()).map_err(|error| format!("Cannot write result: {error}"))
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            let mut stderr = io::stderr().lock();
            let _ = serde_json::to_writer(&mut stderr, &serde_json::json!({ "error": error }));
            let _ = writeln!(stderr);
            ExitCode::FAILURE
        }
    }
}
