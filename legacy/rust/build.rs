use std::fs;
use chrono::Local;

fn main() {
    // Считываем версию из Makefile.version
    let version = fs::read_to_string("Makefile.version")
        .expect("Failed to read version from Makefile.version")
        .trim()
        .to_string();
    
    // Получаем текущую дату сборки
    let build_date = Local::now().format("%Y-%m-%d").to_string();
    
    // Передаем версию и дату сборки как переменные окружения для компилятора
    println!("cargo:rustc-env=TELLO_LIB_VERSION={}", version);
    println!("cargo:rustc-env=TELLO_BUILD_DATE={}", build_date);
    
    // Перекомпилировать при изменении версионного файла
    println!("cargo:rerun-if-changed=Makefile.version");
}