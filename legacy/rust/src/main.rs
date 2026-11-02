/*
 * DJI Tello Drone Controller Library
 *
 * Copyright (c) 2025 aleskin
 *
 * This file is part of dji_tello_lib.
 *
 * dji_tello_lib is free software: you can redistribute it and/or modify
 * it under the terms of the MIT License as published.
 *
 * Created: March 30, 2025
 */

mod tello;
mod tello_movement;
mod command_line;

use std::io;
use tello::Tello;

fn main() -> io::Result<()> {
    // Initialize the drone connection
    println!("Connecting to Tello drone...");
    let mut drone = match Tello::new() {
        Ok(d) => d,
        Err(e) => {
            eprintln!("Failed to initialize drone connection: {}", e);
            return Err(e);
        }
    };
    
    if let Err(e) = drone.connect() {
        eprintln!("Failed to connect to drone: {}", e);
        return Err(e);
    }
    
    // Run the interactive command line interface
    command_line::run_command_line(drone)
}
