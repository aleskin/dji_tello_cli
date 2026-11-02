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

// Movement methods for Tello
use std::io;
use super::tello::Tello;

impl Tello {
    /// Move the drone forward by a specified distance in centimeters
    pub fn forward(&mut self, distance: i32) -> io::Result<()> {
        if distance <= 0 || distance > 500 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Invalid distance value: {}. Should be between 1 and 500 cm.", distance),
            ));
        }
        
        let response = self.send_command(&format!("forward {}", distance))?;
        
        if response != "ok" {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Forward movement command failed: {}", response),
            ));
        }
        
        // Update position tracking
        self.update_position_after_movement("forward", distance);
        
        Ok(())
    }
    
    /// Move the drone backward by a specified distance in centimeters
    pub fn back(&mut self, distance: i32) -> io::Result<()> {
        if distance <= 0 || distance > 500 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Invalid distance value: {}. Should be between 1 and 500 cm.", distance),
            ));
        }
        
        let response = self.send_command(&format!("back {}", distance))?;
        
        if response != "ok" {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Backward movement command failed: {}", response),
            ));
        }
        
        // Update position tracking
        self.update_position_after_movement("back", distance);
        
        Ok(())
    }
    
    /// Move the drone left by a specified distance in centimeters
    pub fn left(&mut self, distance: i32) -> io::Result<()> {
        if distance <= 0 || distance > 500 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Invalid distance value: {}. Should be between 1 and 500 cm.", distance),
            ));
        }
        
        let response = self.send_command(&format!("left {}", distance))?;
        
        if response != "ok" {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Left movement command failed: {}", response),
            ));
        }
        
        // Update position tracking
        self.update_position_after_movement("left", distance);
        
        Ok(())
    }
    
    /// Move the drone right by a specified distance in centimeters
    pub fn right(&mut self, distance: i32) -> io::Result<()> {
        if distance <= 0 || distance > 500 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Invalid distance value: {}. Should be between 1 and 500 cm.", distance),
            ));
        }
        
        let response = self.send_command(&format!("right {}", distance))?;
        
        if response != "ok" {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Right movement command failed: {}", response),
            ));
        }
        
        // Update position tracking
        self.update_position_after_movement("right", distance);
        
        Ok(())
    }
    
    /// Move the drone up by a specified distance in centimeters
    pub fn up(&mut self, distance: i32) -> io::Result<()> {
        if distance <= 0 || distance > 500 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Invalid distance value: {}. Should be between 1 and 500 cm.", distance),
            ));
        }
        
        let response = self.send_command(&format!("up {}", distance))?;
        
        if response != "ok" {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Upward movement command failed: {}", response),
            ));
        }
        
        // Update position tracking
        self.update_position_after_movement("up", distance);
        
        Ok(())
    }
    
    /// Move the drone down by a specified distance in centimeters
    pub fn down(&mut self, distance: i32) -> io::Result<()> {
        if distance <= 0 || distance > 500 {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("Invalid distance value: {}. Should be between 1 and 500 cm.", distance),
            ));
        }
        
        let response = self.send_command(&format!("down {}", distance))?;
        
        if response != "ok" {
            return Err(io::Error::new(
                io::ErrorKind::Other,
                format!("Downward movement command failed: {}", response),
            ));
        }
        
        // Update position tracking
        self.update_position_after_movement("down", distance);
        
        Ok(())
    }
}
