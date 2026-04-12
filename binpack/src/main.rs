use std::{env, fs, process};

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 {
        eprintln!("Usage: {} <input> <output>", args[0]);
        process::exit(1);
    }

    let data = fs::read(&args[1]).unwrap_or_else(|e| {
        eprintln!("Error reading {}: {}", args[1], e);
        process::exit(1);
    });

    if data.len() > 0xFFFF {
        eprintln!("Error: file is {} bytes, max is 65535", data.len());
        process::exit(1);
    }

    let len = data.len() as u16;
    let mut out = vec![
        // Magic
        0x45u8, 0x69, 0x01,
        // Entrypoint
        0x00, 0x04,
        // Section count
        0x01,
        // Section address
        0x00, 0x04,
        // Ram bank
        0xff];
    out.extend_from_slice(&len.to_le_bytes());
    out.extend_from_slice(&data);

    fs::write(&args[2], &out).unwrap_or_else(|e| {
        eprintln!("Error writing {}: {}", args[2], e);
        process::exit(1);
    });
}
